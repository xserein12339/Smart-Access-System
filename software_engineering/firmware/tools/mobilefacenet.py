import os
import glob
import numpy as np
import torch
from torch.utils.data import Dataset, DataLoader
from PIL import Image
import onnx
from esp_ppq.api import espdl_quantize_onnx


# ================= 配置 =================

PTH_PATH = "mobilefacenet.pth"
ONNX_PATH = "mobilefacenet_fixed.onnx"
ESPDL_PATH = "mobilefacenet_int8.espdl"
IMAGE_DIR = "calib_images"

MEAN = [0.5, 0.5, 0.5]
STD = [0.5, 0.5, 0.5]

CALIB_BATCH_SIZE = 16
CALIB_STEPS = 32
INPUT_SHAPE = [1, 3, 112, 112]


# ================= 数据集 =================

class ImageCalibDataset(Dataset):
    def __init__(self, image_dir, mean, std):
        self.image_paths = []
        for ext in ["*.jpg", "*.jpeg", "*.png", "*.bmp"]:
            self.image_paths.extend(glob.glob(os.path.join(image_dir, ext)))
        self.image_paths.sort()
        if len(self.image_paths) == 0:
            raise ValueError(f"在 {image_dir} 中未找到图像！")
        print(f"找到 {len(self.image_paths)} 张校准图像")
        self.mean = torch.tensor(mean, dtype=torch.float32).view(3, 1, 1)
        self.std = torch.tensor(std, dtype=torch.float32).view(3, 1, 1)

    def __len__(self):
        return len(self.image_paths)

    def __getitem__(self, idx):
        img = Image.open(self.image_paths[idx]).convert("RGB")
        if img.size != (112, 112):
            img = img.resize((112, 112), Image.BILINEAR)
        img = np.array(img).astype(np.float32) / 255.0
        img = torch.from_numpy(img).permute(2, 0, 1)
        img = (img - self.mean) / self.std
        return img


def collate_fn(batch):
    return torch.stack(batch, dim=0)


# ================= 主流程 =================

def load_model_cpu(path):
    """强制加载 TorchScript 模型到 CPU"""
    
    # 方法1: torch.jit.load 指定 map_location='cpu'
    try:
        model = torch.jit.load(path, map_location='cpu')
        model.eval()
        print("✅ torch.jit.load(map_location='cpu') 成功")
        return model
    except Exception as e1:
        print(f"方法1失败: {e1}")
    
    # 方法2: torch.load 强制 map_location='cpu'
    try:
        checkpoint = torch.load(path, map_location='cpu')
        if hasattr(checkpoint, 'eval'):
            checkpoint.eval()
        print("✅ torch.load(map_location='cpu') 成功")
        return checkpoint
    except Exception as e2:
        print(f"方法2失败: {e2}")
    
    raise RuntimeError("无法加载模型，请确认 .pth 文件是有效的 TorchScript 模型")


def main():
    # Step 1: 加载模型（强制 CPU）
    print("Step 1: 加载 TorchScript 模型到 CPU")
    model = load_model_cpu(PTH_PATH)

    # 测试推理
    dummy_input = torch.randn(1, 3, 112, 112)
    with torch.no_grad():
        test_output = model(dummy_input)
    print(f"   测试输出形状: {test_output.shape}")

    # Step 2: 导出 ONNX
    print("\nStep 2: 导出 ONNX")
    torch.onnx.export(
        model,
        dummy_input,
        ONNX_PATH,
        opset_version=13,
        input_names=["input"],
        output_names=["output"],
        do_constant_folding=True,
        dynamic_axes=None,
    )

    onnx_model = onnx.load(ONNX_PATH)
    onnx.checker.check_model(onnx_model)

    input_shape = onnx_model.graph.input[0].type.tensor_type.shape
    output_shape = onnx_model.graph.output[0].type.tensor_type.shape
    print(f"✅ ONNX 导出成功")
    print(f"   输入: {[d.dim_value for d in input_shape.dim]}")
    print(f"   输出: {[d.dim_value for d in output_shape.dim]}")

    # Step 3: 量化
    print("\nStep 3: ESP-PPQ 量化")
    dataset = ImageCalibDataset(IMAGE_DIR, MEAN, STD)
    dataloader = DataLoader(
        dataset,
        batch_size=CALIB_BATCH_SIZE,
        shuffle=False,
        num_workers=0,
        collate_fn=collate_fn
    )

    print(f"\n开始量化: {ONNX_PATH} -> {ESPDL_PATH}")
    quant_ppq_graph = espdl_quantize_onnx(
        onnx_import_file=ONNX_PATH,
        espdl_export_file=ESPDL_PATH,
        calib_dataloader=dataloader,
        calib_steps=CALIB_STEPS,
        input_shape=INPUT_SHAPE,
        target="esp32p4",
        num_of_bits=8,
        collate_fn=collate_fn,
        device="cpu",
        error_report=True,
        export_test_values=True,
        verbose=1
    )

    print(f"\n✅ 量化成功！输出: {ESPDL_PATH}")
    print(f"   文件大小: {os.path.getsize(ESPDL_PATH) / 1024:.1f} KB")


if __name__ == "__main__":
    main()