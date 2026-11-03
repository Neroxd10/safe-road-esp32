// ESP32-S3 PIE: eight signed 16-bit PReLU values per iteration.
// All pointers must be 16-byte aligned; positive exponent shift must be zero.
    .section .iram1,"ax"
    .align 4
    .global v7_prelu_simd_row
    .type v7_prelu_simd_row, @function
v7_prelu_simd_row:
    entry sp, 16
    // a2=output, a3=input, a4=slopes, a5=blocks of eight, a6=negative shift
    loopgtz a5, 1f
        EE.VLD.128.IP q0, a3, 16
        EE.VLD.128.IP q1, a4, 16
        EE.VPRELU.S16 q0, q0, q1, a6
        EE.VST.128.IP q0, a2, 16
    1:
    retw
    .size v7_prelu_simd_row, .-v7_prelu_simd_row
