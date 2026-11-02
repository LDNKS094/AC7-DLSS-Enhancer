EXTERN ac7_temporal_original_build:QWORD
EXTERN ac7_temporal_original_color:QWORD
EXTERN ac7_temporal_build:PROC
EXTERN ac7_temporal_color:PROC
EXTERN ac7_frame_original_simulation_end:QWORD
EXTERN ac7_frame_simulation_end:PROC
PUBLIC ac7_temporal_build_thunk
PUBLIC ac7_temporal_color_thunk
PUBLIC ac7_frame_simulation_end_thunk

SAVE_VOLATILE MACRO
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp,80h
    movdqu [rsp+20h],xmm0
    movdqu [rsp+30h],xmm1
    movdqu [rsp+40h],xmm2
    movdqu [rsp+50h],xmm3
    movdqu [rsp+60h],xmm4
    movdqu [rsp+70h],xmm5
ENDM
RESTORE_VOLATILE MACRO
    movdqu xmm0,[rsp+20h]
    movdqu xmm1,[rsp+30h]
    movdqu xmm2,[rsp+40h]
    movdqu xmm3,[rsp+50h]
    movdqu xmm4,[rsp+60h]
    movdqu xmm5,[rsp+70h]
    add rsp,80h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
ENDM
.code
ac7_temporal_build_thunk PROC
    SAVE_VOLATILE
    lea rcx,[rbp-30h]
    mov rdx,rsi
    lea r8,[rsp+134h]
    lea r9,[rbp+148h]
    call ac7_temporal_build
    RESTORE_VOLATILE
    jmp QWORD PTR [ac7_temporal_original_build]
ac7_temporal_build_thunk ENDP
ac7_temporal_color_thunk PROC
    SAVE_VOLATILE
    lea rcx,[rbp-30h]
    call ac7_temporal_color
    RESTORE_VOLATILE
    jmp QWORD PTR [ac7_temporal_original_color]
ac7_temporal_color_thunk ENDP
ac7_frame_simulation_end_thunk PROC
    SAVE_VOLATILE
    call ac7_frame_simulation_end
    RESTORE_VOLATILE
    jmp QWORD PTR [ac7_frame_original_simulation_end]
ac7_frame_simulation_end_thunk ENDP
END
