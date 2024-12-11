    .global __start_custom_data
    .type __start_custom_data, STT_OBJECT
    .global __stop_custom_data
    .type __stop_custom_data, STT_OBJECT
.bss
.align 1024
__start_custom_data: .fill 1024 * 1024
__stop_custom_data:
