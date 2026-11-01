# Enable the Cortex-M4F hardware FPU (hard-float ABI) for every compile and link step.
# PlatformIO's stm32cube builder adds no float-ABI flags, and plain build_flags do not
# reach the linker, so a soft-float libc would be linked. FreeRTOS ARM_CM4F needs hard float.
Import("env")

fpu_flags = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]
env.Append(CCFLAGS=fpu_flags, ASFLAGS=fpu_flags, ASPPFLAGS=fpu_flags, LINKFLAGS=fpu_flags)
