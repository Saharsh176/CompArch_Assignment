# Test load-use hazard stalling and branch flushes
.text
main:
    addi x1, x0, 10      # x1 = 10
    sw   x1, 0(x0)       # store 10 to memory addr 0
    
    # This load-use hazard should trigger a pipeline stall:
    lw   x2, 0(x0)       # x2 = mem[0] (load)
    addi x3, x2, 5       # x3 = x2 + 5 (uses x2 immediately after load)

    # Test branch flush
    addi x4, x0, 0       # x4 = 0
    beq  x1, x4, skip    # should not be taken
    addi x5, x0, 99      # x5 = 99 (should execute)
    j    end

skip:
    addi x5, x0, 42      # should be flushed if branch was taken

end:
    li   a7, 10          # exit ecall
    ecall