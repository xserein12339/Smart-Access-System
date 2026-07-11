import onnx
import numpy as np
from onnx import numpy_helper


def get_initializer(graph, name):
    for init in graph.initializer:
        if init.name == name:
            return numpy_helper.to_array(init)
    return None


def find_node_by_output(graph, output_name):
    for node in graph.node:
        if output_name in node.output:
            return node
    return None


def fuse_bn1d_into_matmul(onnx_path, output_path):
    model = onnx.load(onnx_path)
    graph = model.graph
    
    nodes_to_remove = []
    init_names_to_remove = set()
    
    for i, node in enumerate(graph.node):
        if node.op_type != "BatchNormalization":
            continue
        
        bn_name = node.name
        bn_inputs = list(node.input)
        bn_outputs = list(node.output)
        
        x_input = bn_inputs[0]
        scale_name = bn_inputs[1]
        b_name = bn_inputs[2]
        mean_name = bn_inputs[3]
        var_name = bn_inputs[4]
        y_output = bn_outputs[0]
        
        # 读取 BN 参数
        scale = get_initializer(graph, scale_name)
        beta = get_initializer(graph, b_name)
        mean = get_initializer(graph, mean_name)
        var = get_initializer(graph, var_name)
        
        if scale is None:
            print(f"跳过 {bn_name}: 找不到 initializer")
            continue
        
        epsilon = 1e-5
        for attr in node.attribute:
            if attr.name == "epsilon":
                epsilon = attr.f
        
        std = np.sqrt(var + epsilon)
        gamma = scale / std
        
        # 找到前驱节点
        prev_node = find_node_by_output(graph, x_input)
        if prev_node is None:
            print(f"跳过 {bn_name}: 前驱是 graph input")
            continue
        
        print(f"🔍 找到 BN 节点: {bn_name}")
        print(f"🔍 前驱: {prev_node.name} (op: {prev_node.op_type})")
        
        # 情况1: 前驱是 MatMul (Linear 被拆成了 MatMul + Add)
        if prev_node.op_type == "MatMul":
            # MatMul 输入: A, B
            # B 是权重 [in_features, out_features]
            w_name = prev_node.input[1]
            w = get_initializer(graph, w_name)
            
            if w is None:
                print(f"  ❌ 找不到 MatMul 权重 {w_name}")
                continue
            
            # 融合: w_new = w * (gamma / std)
            # w 形状: [in_features, out_features]
            w_new = (w * gamma.reshape(1, -1)).astype(np.float32)
            
            # 更新权重 initializer
            for init in graph.initializer:
                if init.name == w_name:
                    init.CopyFrom(numpy_helper.from_array(w_new, w_name))
                    break
            
            # 检查是否有 Add 节点（偏置）
            # MatMul 后面可能直接是 BN，也可能中间有 Add
            # 如果有 Add，需要把 BN 的 bias 合并到 Add 里
            add_node = None
            for j, later in enumerate(graph.node):
                if later.op_type == "Add" and x_input in later.output:
                    # 检查这个 Add 是否在 MatMul 和 BN 之间
                    # 简化：如果 Add 的输出是 BN 的输入，那就合并
                    if later.output[0] == x_input:
                        add_node = later
                        break
            
            if add_node:
                # Add 节点: input0 + input1 = output
                # input1 通常是 bias [out_features]
                bias_name = add_node.input[1] if add_node.input[0] == x_input else add_node.input[0]
                bias = get_initializer(graph, bias_name)
                
                if bias is not None:
                    bias_new = ((bias - mean) * gamma + beta).astype(np.float32)
                    for init in graph.initializer:
                        if init.name == bias_name:
                            init.CopyFrom(numpy_helper.from_array(bias_new, bias_name))
                            break
                    print(f"  ✅ 融合 MatMul + Add + BN")
                else:
                    print(f"  ⚠️ Add 的输入不是 initializer，跳过")
                    continue
                
                # 修改 Add 的输出为 BN 的输出
                add_node.output[0] = y_output
            else:
                # 没有 Add，MatMul 直接接 BN
                # 需要给 MatMul 添加一个 Add 节点作为新的偏置
                # 或者更简单：把 BN 的变换吸收到一个新的 Add 里
                new_bias = ((-mean) * gamma + beta).astype(np.float32)
                new_bias_name = f"{bn_name}_bias_fused"
                graph.initializer.append(numpy_helper.from_array(new_bias, new_bias_name))
                
                # 创建 Add 节点
                add_node = onnx.helper.make_node(
                    "Add",
                    inputs=[x_input, new_bias_name],
                    outputs=[y_output],
                    name=f"{bn_name}_add_fused"
                )
                graph.node.insert(i + 1, add_node)  # 在 BN 后面插入 Add
                print(f"  ✅ 融合 MatMul + BN -> MatMul + Add")
            
            # 删除 BN 节点
            nodes_to_remove.append(i)
            init_names_to_remove.update([scale_name, b_name, mean_name, var_name])
            
        # 情况2: 前驱是 Gemm
        elif prev_node.op_type == "Gemm":
            w_name = prev_node.input[1]
            has_bias = len(prev_node.input) > 2
            bias_name = prev_node.input[2] if has_bias else None
            
            w = get_initializer(graph, w_name)
            bias = get_initializer(graph, bias_name) if has_bias else np.zeros_like(mean)
            
            if w is None:
                print(f"  ❌ 找不到 Gemm 权重")
                continue
            
            # 检查 transB
            transB = 0
            for attr in prev_node.attribute:
                if attr.name == "transB":
                    transB = attr.i
            
            if transB == 0:
                w_new = (w * gamma.reshape(1, -1)).astype(np.float32)
            else:
                w_new = (w * gamma.reshape(-1, 1)).astype(np.float32)
            
            bias_new = ((bias - mean) * gamma + beta).astype(np.float32)
            
            for init in graph.initializer:
                if init.name == w_name:
                    init.CopyFrom(numpy_helper.from_array(w_new, w_name))
                elif has_bias and init.name == bias_name:
                    init.CopyFrom(numpy_helper.from_array(bias_new, bias_name))
            
            if not has_bias:
                new_bias_name = f"{prev_node.name}_bias_fused"
                graph.initializer.append(numpy_helper.from_array(bias_new, new_bias_name))
                prev_node.input.append(new_bias_name)
            
            prev_node.output[0] = y_output
            nodes_to_remove.append(i)
            init_names_to_remove.update([scale_name, b_name, mean_name, var_name])
            print(f"  ✅ 融合 Gemm + BN")
            
        else:
            print(f"  ⚠️ 不支持的前驱类型: {prev_node.op_type}，跳过")
    
    if len(nodes_to_remove) == 0:
        print("❌ 没有找到可融合的 BN")
        return False
    
    # 删除 BN 节点（从后往前）
    for i in sorted(nodes_to_remove, reverse=True):
        del graph.node[i]
    
    # 删除 BN 相关的 initializer（使用 ClearField + extend）
    remaining_inits = [init for init in graph.initializer if init.name not in init_names_to_remove]
    graph.ClearField("initializer")
    graph.initializer.extend(remaining_inits)
    
    # 清理不再使用的输入（可选）
    onnx.save(model, output_path)
    print(f"\n✅ 修复完成！保存到: {output_path}")
    
    # 验证
    try:
        onnx.checker.check_model(output_path)
        print("✅ ONNX 验证通过")
    except Exception as e:
        print(f"⚠️ 验证警告: {e}")
    
    return True


if __name__ == "__main__":
    fuse_bn1d_into_matmul("mobilefacenet.onnx", "mobilefacenet_fixed.onnx")