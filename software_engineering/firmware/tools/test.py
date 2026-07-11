import os
import numpy as np
import torch
import onnxruntime as ort
from torch.utils.data import Dataset, DataLoader
from PIL import Image
from tqdm import tqdm

from esp_ppq.api import espdl_quantize_onnx
from esp_ppq.executor import TorchExecutor  # ✅ 已修正导入路径

# ================= 配置 =================
ONNX_PATH = "mobilefacenet_fixed.onnx"
ESPDL_PATH = "mobilefacenet_int8.espdl"
IMAGE_DIR = "calib_images"          # 校准集（用于重建INT8 Graph）
PAIRS_FILE = "test_images/val_pairs.txt"        # ✅ 验证集 pairs 文件
IMAGE_ROOT = "test_images"           # ✅ pairs 中图片路径的根目录

MEAN = [0.5, 0.5, 0.5]
STD = [0.5, 0.5, 0.5]
CALIB_BATCH_SIZE = 16
CALIB_STEPS = 32
INPUT_SHAPE = [1, 3, 112, 112]
DEVICE = "cpu"

# ================= 数据集定义 =================

class FaceCalibDataset(Dataset):
    """校准数据集（仅用于重建INT8 Executor）"""
    def __init__(self, image_dir, mean, std):
        self.image_paths = []
        for ext in ["*.jpg", "*.jpeg", "*.png", "*.bmp"]:
            self.image_paths.extend(
                [p for p in __import__('glob').glob(os.path.join(image_dir, ext))]
            )
        self.image_paths.sort()
        if not self.image_paths:
            raise ValueError(f"在 {image_dir} 中未找到图像！")
        print(f"  📂 校准集: {len(self.image_paths)} 张图像")
        self.mean = torch.tensor(mean, dtype=torch.float32).view(3, 1, 1)
        self.std = torch.tensor(std, dtype=torch.float32).view(3, 1, 1)

    def __len__(self): return len(self.image_paths)

    def __getitem__(self, idx):
        img = Image.open(self.image_paths[idx]).convert("RGB")
        if img.size != (112, 112):
            img = img.resize((112, 112), Image.BILINEAR)
        img = np.array(img).astype(np.float32) / 255.0
        img = torch.from_numpy(img).permute(2, 0, 1)
        return (img - self.mean) / self.std


class PairDataset(Dataset):
    """✅ 正负样本对验证数据集（适配Tab分隔 + 绝对路径格式）"""
    def __init__(self, pairs_file, mean, std):
        self.pairs = []
        with open(pairs_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                # ✅ 关键修改1: 使用 \t 分隔，而非默认的空白符
                parts = line.split('\t')
                if len(parts) >= 3:
                    label = int(parts[0])
                    img1_path = parts[1]
                    img2_path = parts[2]
                    self.pairs.append((img1_path, img2_path, label))

        if not self.pairs:
            raise ValueError(f"在 {pairs_file} 中未解析到有效样本对！")

        pos = sum(1 for p in self.pairs if p[2] == 1)
        neg = len(self.pairs) - pos
        print(f"  📂 验证集: {len(self.pairs)} 对 (正:{pos} / 负:{neg})")

        self.mean = torch.tensor(mean, dtype=torch.float32).view(3, 1, 1)
        self.std = torch.tensor(std, dtype=torch.float32).view(3, 1, 1)

    def _load_img(self, path):
        # ✅ 关键修改2: pairs文件中已是绝对路径，直接使用，不再拼接 IMAGE_ROOT
        img = Image.open(path).convert("RGB")
        if img.size != (112, 112):
            img = img.resize((112, 112), Image.BILINEAR)
        img = np.array(img).astype(np.float32) / 255.0
        img = torch.from_numpy(img).permute(2, 0, 1)
        return (img - self.mean) / self.std

    def __len__(self):
        return len(self.pairs)

    def __getitem__(self, idx):
        p1, p2, label = self.pairs[idx]
        return self._load_img(p1), self._load_img(p2), label


def pair_collate_fn(batch):
    imgs1, imgs2, labels = zip(*batch)
    return torch.stack(imgs1), torch.stack(imgs2), torch.tensor(labels)


def calib_collate_fn(batch):
    # ✅ 兼容 DataLoader 自动 collate 和手动传入 list 两种情况
    if isinstance(batch, torch.Tensor):
        return batch
    return torch.stack(batch, dim=0)

# ================= Executor 构建 =================

def make_int8_executor():
    dataset = FaceCalibDataset(IMAGE_DIR, MEAN, STD)
    loader = DataLoader(dataset, batch_size=CALIB_BATCH_SIZE,
                        shuffle=False, num_workers=0, collate_fn=calib_collate_fn)
    print("  ⏳ 重新量化以获取内存Graph...")
    quant_graph = espdl_quantize_onnx(
        onnx_import_file=ONNX_PATH, espdl_export_file=ESPDL_PATH,
        calib_dataloader=loader, calib_steps=CALIB_STEPS,
        input_shape=INPUT_SHAPE, target="esp32p4", num_of_bits=8,
        collate_fn=calib_collate_fn, device=DEVICE,
        error_report=False, export_test_values=False, verbose=0
    )
    executor = TorchExecutor(graph=quant_graph, fp16_mode=False, device=DEVICE)
    print("  ✅ INT8 TorchExecutor 就绪")
    return executor


def make_fp32_executor():
    session = ort.InferenceSession(ONNX_PATH, providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name
    print("  ✅ FP32 ONNXRuntime 就绪")
    return session, input_name


# ================= 评估与指标计算 =================

@torch.no_grad()
def evaluate_pairs(executor_type, executor, dataloader, desc="Evaluating"):
    all_sims, all_labels = [], []
    for imgs1, imgs2, labels in tqdm(dataloader, desc=desc):
        if executor_type == "int8":
            emb1 = executor(imgs1)
            emb2 = executor(imgs2)
            if isinstance(emb1, list): emb1 = emb1[0]
            if isinstance(emb2, list): emb2 = emb2[0]
            emb1, emb2 = emb1.cpu().numpy(), emb2.cpu().numpy()
        else:
            session, input_name = executor
            emb1 = session.run(None, {input_name: imgs1.numpy()})[0]
            emb2 = session.run(None, {input_name: imgs2.numpy()})[0]

        sims = np.sum(emb1 * emb2, axis=1) / (
            np.linalg.norm(emb1, axis=1) * np.linalg.norm(emb2, axis=1) + 1e-8
        )
        all_sims.append(sims)
        all_labels.append(labels.numpy())
    return np.concatenate(all_sims), np.concatenate(all_labels)


def compute_tar_at_far(sims, labels, target_fars=[1e-4, 1e-3, 1e-2]):
    """计算 TAR@FAR（人脸识别量化评估核心指标）"""
    pos_sims = sims[labels == 1]
    neg_sims = sims[labels == 0]
    results = {}
    for far in target_fars:
        threshold = np.percentile(neg_sims, 100 * (1 - far))
        tar = np.mean(pos_sims >= threshold)
        results[f"TAR@FAR={far:.0e}"] = tar
    return results


# ================= 主流程 =================

def main():
    print("=" * 60)
    print("MobileFaceNet 量化评估（FP32 vs INT8 | Pairs模式）")
    print("=" * 60)

    print("\n[1/4] 加载验证样本对...")
    val_dataset = PairDataset(PAIRS_FILE, MEAN, STD)
    val_loader = DataLoader(val_dataset, batch_size=1, shuffle=False, num_workers=0, collate_fn=pair_collate_fn)
    print("\n[2/4] 构建 FP32 基准...")
    fp32_exec = make_fp32_executor()

    print("\n[3/4] 构建 INT8 模型...")
    int8_exec = make_int8_executor()

    print("\n[4/4] 执行精度对比...")
    fp32_sims, labels = evaluate_pairs("fp32", fp32_exec, val_loader, "FP32")
    int8_sims, _      = evaluate_pairs("int8", int8_exec, val_loader, "INT8")

    # ================= 结果输出 =================
    print("\n" + "=" * 60)
    print("📊 评估结果")
    print("=" * 60)

    fp32_metrics = compute_tar_at_far(fp32_sims, labels)
    int8_metrics = compute_tar_at_far(int8_sims, labels)

    print(f"\n{'指标':<16} {'FP32':>8} {'INT8':>8} {'Drop':>8}")
    print("-" * 44)
    for k in fp32_metrics:
        drop = fp32_metrics[k] - int8_metrics[k]
        flag = "⚠️" if drop > 0.02 else "✅"
        print(f"{k:<16} {fp32_metrics[k]:>8.4f} {int8_metrics[k]:>8.4f} {drop:>+8.4f} {flag}")

    # 补充：逐对余弦相似度统计
    cos_diff = fp32_sims - int8_sims
    print(f"\n🔹 逐对相似度差异 (FP32 - INT8):")
    print(f"   Mean: {cos_diff.mean():+.6f} | Std: {cos_diff.std():.6f} | Max: {np.abs(cos_diff).max():.6f}")
    print("\n✅ 评估完成！")


if __name__ == "__main__":
    main()