"""Use explicit NCHW/NHWC boundaries for each Conv in the V7 expression export.

Keep the original ONNX layout everywhere else. Avoid the exporter's propagated
layout/fusion passes, which produced incompatible broadcast shapes for V7.
This intentionally trades extra transpose operations for simple layout semantics.
"""
from esp_ppq.IR.quantize import QuantableOperation
from esp_ppq.core import OperationQuantizationConfig
from esp_ppq.parser.espdl.espdl_typedef import ExporterPatternInfo
from esp_ppq.parser.espdl.espdl_graph_utils import insert_transpose_node


def reset_graph_layout(graph):
    info=ExporterPatternInfo()
    for var in graph.variables.values():
        if not var.is_parameter:
            info.add_var_permute(var.name,list(range(len(var.shape))))
    for op in list(graph.topological_sort()):
        if op.type!='Conv':
            continue
        rank=len(op.inputs[0].shape)
        assert rank in (3,4)
        forward=[0,2,3,1] if rank==4 else [0,2,1]
        backward=[0,3,1,2] if rank==4 else [0,2,1]
        # Physically insert NCHW -> NHWC. Metadata stays identity because
        # the newly inserted intermediate shape is already the actual shape.
        insert_transpose_node(graph,op.inputs[0],op,forward)
        original_output=op.outputs[0]
        original_shape=original_output.shape.copy()
        transpose=graph.create_operation(op_type='Transpose',attributes={'perm':backward})
        if isinstance(op,QuantableOperation):
            q=op.output_quant_config[0]
            transpose=QuantableOperation(transpose,OperationQuantizationConfig([q],[q]),op.platform)
            graph.operations[transpose.name]=transpose
        graph.insert_op_after(transpose,op)
        intermediate=op.outputs[0]
        intermediate.shape=[original_shape[i] for i in forward]
        intermediate.dtype=original_output.dtype
        intermediate.is_parameter=False
        info.add_var_permute(intermediate.name,list(range(rank)))
        info.add_var_permute(original_output.name,list(range(rank)))


def install():
    import esp_ppq.parser.espdl_exporter as exporter
    exporter.reset_graph_layout=reset_graph_layout
