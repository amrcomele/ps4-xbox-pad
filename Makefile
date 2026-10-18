TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
GH_SDK      := $(GOLDHEN_SDK)
TARGET      := xbox_pad.prx
OBJS        := main.o

CC          := clang
LD          := ld.lld

CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -O2 -Wall \
               -I"$(TOOLCHAIN)/include" \
               -I"$(GH_SDK)/include"

LDFLAGS     := -m elf_x86_64 -pie --script "$(TOOLCHAIN)/link.x" \
               -e _init --eh-frame-hdr \
               -L"$(TOOLCHAIN)/lib" \
               -L"$(GH_SDK)/build" \
               -L"$(GH_SDK)" \
               -lGoldHEN_Hook -lScePad -lScePosix -lkernel

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(LD) $(OBJS) -o $(TARGET).elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/linux/create-fself -in=$(TARGET).elf -out=$(TARGET).oelf --lib=$(TARGET) --paid 0x3800000000000011
	mv $(TARGET).oelf $(TARGET)