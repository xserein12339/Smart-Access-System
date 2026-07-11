import torch
import torch.nn as nn
from mobilefacenet import MobileFaceNet


class CleanMobileFaceNet(nn.Module):
    """包装器：从TorchScript模型截取到conv_6_dw的输出，重新接上正确的flatten"""
    def __init__(self, jit_model):
        super().__init__()
        self.jit_model = jit_model
        # 冻结原始模型
        for p in self.jit_model.parameters():
            p.requires_grad = False

    def forward(self, x):
        # 遍历TorchScript模块，找到conv_6_dw (即最后一个DepthWise) 的输出
        # conv_6_dw 输出 shape: [B, 512, 7, 7]
        # 注意：TorchScript模型的子模块命名可能与源码不同，需通过调试确认
        # 这里使用通用策略：执行到倒数第3个节点（跳过sep/dw/flatten）
        
        # 更稳健的方式：直接利用原模型forward，但在flatten前拦截
        # 由于TorchScript无法轻易拦截，我们采用trace方式重建干净图
        
        # 实际上，MobileFaceNet的conv_6_dw输出是[B,512,7,7]
        # 我们需要找到这个中间tensor
        raise NotImplementedError("请使用下方的trace方案")


def export_clean_onnx(jit_path, output_path):
    """通过trace方式重建干净的ONNX，彻底消除尾部孤立BN"""
    
    # 1. 加载TorchScript模型
    print("📌 加载 TorchScript 模型...")
    jit_model = torch.jit.load(jit_path, map_location="cpu")
    jit_model.eval()
    
    # 2. 用dummy输入trace，获取中间特征图尺寸
    dummy = torch.randn(1, 3, 112, 112)
    with torch.no_grad():
        full_out = jit_model(dummy)
    print(f"   原始输出 shape: {full_out.shape}")  # 应为 [1, 512]
    
    # 3. 构建干净的包装模型
    #    关键：TorchScript模型的forward已经包含了有问题的BN
    #    我们需要找到一个方法绕过它
    #    
    #    最佳方案：直接用state_dict重新加载（如果能获取的话）
    #    备选方案：对TorchScript模型做graph surgery
    
    # 由于TorchScript graph surgery复杂度高，这里采用最实用的方案：
    # 检查TorchScript模型是否可以提取子模块
    print("🔍 分析 TorchScript 模型结构...")
    print(jit_model.graph)
    
    # ⚠️ 请运行此脚本后，将打印的graph贴出来
    # 我们需要根据实际graph确定如何截取
    
    raise RuntimeError(
        "请先运行此脚本查看graph输出，\n"
        "然后将graph内容反馈给我，\n"
        "我将据此编写精确的graph surgery代码。"
    )


if __name__ == "__main__":
    CHECKPOINT_PATH = "mobilefacenet.pth"
    OUTPUT_ONNX = "mobilefacenet_sim.onnx"
    export_clean_onnx(CHECKPOINT_PATH, OUTPUT_ONNX)