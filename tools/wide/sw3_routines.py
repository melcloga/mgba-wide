from tasm import assemble
MOD = 0x080B3DA0
DRAWRECT = 0x080AA418
ROWDRAW = 0x080A9ED8

full_redraw = f'''
    push {{r4-r7, lr}}
    sub sp, #24
    mov r7, r0
    str r1, [sp, #16]
    ldrh r4, [r7, #8]
    sub r4, #4
    mov r6, #39
loop:
    ldrh r0, [r7, #24]
    mov r5, r4
    and r5, r0
    add r3, r0, #1
    sub r3, r3, r5
    cmp r3, r6
    ble c1
    mov r3, r6
c1:
    add r2, sp, #12
    strh r3, [r2, #0]
    mov r1, #21
    strh r1, [r2, #2]
    add r2, sp, #8
    strh r5, [r2, #0]
    ldrh r0, [r7, #10]
    ldrh r1, [r7, #26]
    and r1, r0
    strh r1, [r2, #2]
    add r2, sp, #4
    strh r4, [r2, #0]
    strh r0, [r2, #2]
    mov r0, r7
    add r0, #57
    ldrb r0, [r0, #0]
    cmp r0, #0
    beq nowrap
    mov r0, r4
    mov r1, #0
    ldrsh r1, [r7, r1]
    bl {MOD:#x}
    add r2, sp, #4
    strh r0, [r2, #0]
    mov r1, #0
    ldrsh r1, [r7, r1]
    sub r1, r1, r0
    add r2, sp, #12
    ldrh r3, [r2, #0]
    cmp r3, r1
    ble c2
    strh r1, [r2, #0]
c2:
    ldrh r0, [r7, #10]
    mov r1, #2
    ldrsh r1, [r7, r1]
    bl {MOD:#x}
    add r2, sp, #4
    strh r0, [r2, #2]
nowrap:
    ldr r0, [sp, #16]
    str r0, [sp, #0]
    mov r0, r7
    add r1, sp, #8
    add r2, sp, #4
    add r3, sp, #12
    bl {DRAWRECT:#x}
    add r2, sp, #12
    ldrh r3, [r2, #0]
    add r4, r4, r3
    sub r6, r6, r3
    cmp r6, #0
    bgt loop
    add sp, #24
    pop {{r4-r7}}
    pop {{r0}}
    bx r0
'''

def row_draw(wrap):
    s = f'''
    push {{r4-r7, lr}}
    sub sp, #16
    mov r4, r0
    mov r5, r1
    mov r6, r2
    mov r7, #39
    ldrh r0, [r4, #8]
    sub r0, #4
    ldrh r1, [r4, #10]
    cmp r6, #0
    ble c1
    add r1, #20
c1:
    cmp r5, #0
    beq c2
    mov r7, #38
    cmp r5, #0
    bge c2
    add r0, #1
c2:
    add r2, sp, #8
    strh r0, [r2, #0]
    strh r1, [r2, #2]
    ldrh r3, [r4, #24]
    and r0, r3
    ldrh r3, [r4, #26]
    and r1, r3
    add r2, sp, #12
    strh r0, [r2, #0]
    strh r1, [r2, #2]
'''
    if wrap:
        s += f'''
    add r2, sp, #8
    ldrh r0, [r2, #0]
    mov r1, #0
    ldrsh r1, [r4, r1]
    bl {MOD:#x}
    add r2, sp, #8
    strh r0, [r2, #0]
    ldrh r0, [r2, #2]
    mov r1, #2
    ldrsh r1, [r4, r1]
    bl {MOD:#x}
    add r2, sp, #8
    strh r0, [r2, #2]
'''
    s += f'''
    ldr r1, [sp, #8]
    ldr r2, [sp, #12]
    str r5, [sp, #0]
    str r6, [sp, #4]
    mov r0, r4
    mov r3, r7
    bl {ROWDRAW:#x}
    add sp, #16
    pop {{r4-r7}}
    pop {{r0}}
    bx r0
'''
    return s

ROUTINES = [
    (0x080AA4C4, 0x080AA596, full_redraw, 'full layer redraw: 39 columns from camera - 4 (both layer kinds)'),
    (0x080AA598, 0x080AA59A, 'b 0x080AA4C4', 'wrapping-layer full redraw: use the routine above'),
    (0x080AA15C, 0x080AA1D2, row_draw(False), 'row streaming: 39 columns from camera - 4'),
    (0x080AA1D4, 0x080AA2B6, row_draw(True), 'row streaming, wrapping layers: 39 columns from camera - 4'),
]
