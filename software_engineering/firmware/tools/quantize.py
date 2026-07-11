import os
import glob
import numpy as np
import torch
from torch.utils.data import Dataset, DataLoader
from PIL import Image
from esp_ppq.api import espdl_quantize_onnx


# ================= 配置 =================
ONNX_PATH = "mobilefacenet_fixed.onnx"   # ✅ 使用修复后的文件
ESPDL_PATH = "mobilefacenet_int8.espdl"
IMAGE_DIR = "calib_images"

# 预处理参数（MobileFaceNet 常用：归一化到 [-1, 1]）
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
    # 如果已经是 Tensor，直接返回
    if isinstance(batch, torch.Tensor):
        return batch
    # 如果是单个元素，包装成 batch
    if isinstance(batch, (list, tuple)) and len(batch) == 1:
        return batch[0].unsqueeze(0)
    return torch.stack(batch, dim=0)

# ================= 主流程 =================
def main():
    print("开始量化...")
    dataset = ImageCalibDataset(IMAGE_DIR, MEAN, STD)
    dataloader = DataLoader(
        dataset,
        batch_size=CALIB_BATCH_SIZE,
        shuffle=False,
        num_workers=0,
        collate_fn=collate_fn
    )

    print(f"\n量化: {ONNX_PATH} -> {ESPDL_PATH}")
    print(f"输入: {INPUT_SHAPE} | 目标: esp32p4 | INT8")

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

    print(f"\n✅ 量化成功！")
    print(f"   输出: {ESPDL_PATH}")
    print(f"   大小: {os.path.getsize(ESPDL_PATH) / 1024:.1f} KB")
    print(f"\n下一步: 将 {ESPDL_PATH} 拷贝到 ESP-IDF 工程中部署")


if __name__ == "__main__":
    main()