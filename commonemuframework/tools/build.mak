# PSP Software Development Kit - http://www.pspdev.org
# -----------------------------------------------------------------------
# Licensed under the BSD license, see LICENSE in PSPSDK root for details.
#
# build.mak - Base makefile for projects using PSPSDK.
#
# Copyright (c) 2005 Marcus R. Brown <mrbrown@ocgnet.org>
# Copyright (c) 2005 James Forshaw <tyranid@gmail.com>
# Copyright (c) 2005 John Kelley <ps2dev@kelley.ca>
#
# $Id: build.mak 2333 2007-10-31 19:37:40Z tyranid $

# Note: The PSPSDK make variable must be defined before this file is included.
ifeq ($(PSPSDK),)
$(error $$(PSPSDK) is undefined.  Use "PSPSDK := $$(shell psp-config --pspsdk-path)" in your Makefile)
endif

ifdef BUILD_DIR
#Rename objects to their respective folders
OBJS := $(patsubst %.o,$(BUILD_DIR)/%.o,$(subst ../,___/,$(OBJS)))
endif

# OS tools
CP       = $(shell psp-config --pspdev-path)/bin/cp
RM       = $(shell psp-config --pspdev-path)/bin/rm
MKDIR    = $(shell psp-config --pspdev-path)/bin/mkdir
TRUE    = $(shell psp-config --pspdev-path)/bin/true

CC       = psp-gcc
CXX      = psp-g++
GDC      = psp-gdc
AS       = psp-gcc
LD       = psp-gcc
AR       = psp-ar
RANLIB   = psp-ranlib
STRIP    = psp-strip
MKSFO    = mksfo
PACK_PBP = pack-pbp
FIXUP    = psp-fixup-imports
ENC      = PrxEncrypter

# Add in PSPSDK includes and libraries.
INCDIR   := $(INCDIR) . $(PSPSDK)/include
LIBDIR   := $(LIBDIR) . $(PSPSDK)/lib

CFLAGS   := $(addprefix -I,$(INCDIR)) $(CFLAGS)
CXXFLAGS := $(CFLAGS) $(CXXFLAGS)
ASFLAGS  := $(CFLAGS) $(ASFLAGS)

ifeq ($(PSP_LARGE_MEMORY),1)
MKSFO = mksfoex -d MEMSIZE=1
endif

ifeq ($(PSP_FW_VERSION),)
PSP_FW_VERSION=150
endif

CFLAGS += -D_PSP_FW_VERSION=$(PSP_FW_VERSION)
CXXFLAGS += -D_PSP_FW_VERSION=$(PSP_FW_VERSION)

# Objective-C selection. All Objective C code must be linked against libobjc.a
ifeq ($(USE_OBJC),1)
LIBS     := $(LIBS) -lobjc
endif

ifeq ($(BUILD_PRX),1)
LDFLAGS  := $(addprefix -L,$(LIBDIR)) -specs=$(PSPSDK)/lib/prxspecs -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx $(LDFLAGS)
EXTRA_CLEAN += $(TARGET).elf
# Setup default exports if needed
ifdef PRX_EXPORTS
EXPORT_OBJ=$(patsubst %.exp,%.o,$(PRX_EXPORTS))
EXTRA_CLEAN += $(EXPORT_OBJ)
else 
EXPORT_OBJ=$(PSPSDK)/lib/prxexports.o
endif
else
LDFLAGS  := $(addprefix -L,$(LIBDIR)) $(LDFLAGS)
endif

# Library selection.  By default we link with Newlib's libc.  Allow the
# user to link with PSPSDK's libc if USE_PSPSDK_LIBC is set to 1.

ifeq ($(USE_KERNEL_LIBC),1)
# Use the PSP's kernel libc
PSPSDK_LIBC_LIB =
CFLAGS := -I$(PSPSDK)/include/libc $(CFLAGS)
else
ifeq ($(USE_PSPSDK_LIBC),1)
# Use the pspsdk libc
PSPSDK_LIBC_LIB = -lpsplibc
CFLAGS := -I$(PSPSDK)/include/libc $(CFLAGS)
else
# Use newlib (urgh)
PSPSDK_LIBC_LIB = -lc
endif
endif


# Link with following default libraries.  Other libraries should be specified in the $(LIBS) variable.
# TODO: This library list needs to be generated at configure time.
#
ifeq ($(USE_KERNEL_LIBS),1)
PSPSDK_LIBS = -lpspdebug -lpspdisplay_driver -lpspctrl_driver -lpspsdk
LIBS     := $(LIBS) $(PSPSDK_LIBS) $(PSPSDK_LIBC_LIB) -lpspkernel
else
ifeq ($(USE_USER_LIBS),1)
PSPSDK_LIBS = -lpspdebug -lpspdisplay -lpspge -lpspctrl -lpspsdk
LIBS     := $(LIBS) $(PSPSDK_LIBS) $(PSPSDK_LIBC_LIB) -lpspnet \
			-lpspnet_inet -lpspnet_apctl -lpspnet_resolver -lpsputility \
			-lpspuser
else
PSPSDK_LIBS = -lpspdebug -lpspdisplay -lpspge -lpspctrl -lpspsdk
LIBS     := $(LIBS) $(PSPSDK_LIBS) $(PSPSDK_LIBC_LIB) -lpspnet \
			-lpspnet_inet -lpspnet_apctl -lpspnet_resolver -lpsputility \
			-lpspuser -lpspkernel
endif
endif

# Define the overridable parameters for EBOOT.PBP
ifndef PSP_EBOOT_TITLE
PSP_EBOOT_TITLE = $(TARGET)
endif

ifndef PSP_EBOOT_SFO
PSP_EBOOT_SFO = PARAM.SFO
endif


TARGET_EBOOT_SFO := $(PSP_EBOOT_SFO)
ifdef BUILD_DIR
TARGET_EBOOT_SFO := $(BUILD_DIR)/$(PSP_EBOOT_SFO)
endif

ifndef PSP_EBOOT_ICON
PSP_EBOOT_ICON = NULL
endif

ifndef PSP_EBOOT_ICON1
PSP_EBOOT_ICON1 = NULL
endif

ifndef PSP_EBOOT_UNKPNG
PSP_EBOOT_UNKPNG = NULL
endif

ifndef PSP_EBOOT_PIC1
PSP_EBOOT_PIC1 = NULL
endif

ifndef PSP_EBOOT_SND0
PSP_EBOOT_SND0 = NULL
endif

ifndef PSP_EBOOT_PSAR
PSP_EBOOT_PSAR = NULL
endif

ifndef PSP_EBOOT
PSP_EBOOT = EBOOT.PBP
endif

ifeq ($(BUILD_PRX),1)
ifneq ($(TARGET_LIB),)
$(error TARGET_LIB should not be defined when building a prx)
else
FINAL_TARGET = $(TARGET).prx
endif
else
ifneq ($(TARGET_LIB),)
FINAL_TARGET = $(TARGET_LIB)
else
FINAL_TARGET = $(TARGET).elf
endif
endif

FINAL_TARGET_BUILD := $(FINAL_TARGET)
TARGET_EBOOT := $(PSP_EBOOT)

ifdef BUILD_DIR
FINAL_TARGET_BUILD := $(BUILD_DIR)/$(FINAL_TARGET)
TARGET_BUILD := $(BUILD_DIR)/$(TARGET)
TARGET_EBOOT := $(BUILD_DIR)/$(PSP_EBOOT)
EXTRA_TARGETS_EBOOT = $(BUILD_DIR)/$(EXTRA_TARGETS)
endif

ifndef BUILD_DIR
psp: $(EXTRA_TARGETS) $(FINAL_TARGET_BUILD)
else
psp: $(EXTRA_TARGETS_EBOOT) $(FINAL_TARGET_BUILD)
endif

#kxploit, not yet done!
kxploit: $(TARGET).elf $(PSP_EBOOT_SFO)
	@echo Compiling kxploit...
	@$(MKDIR) -p "$(TARGET)"
	@$(STRIP) $(TARGET).elf -o $(TARGET)/$(PSP_EBOOT)
	@$(MKDIR) -p "$(TARGET)%"
	@$(PACK_PBP) "$(TARGET)%/$(PSP_EBOOT)" $(PSP_EBOOT_SFO) $(PSP_EBOOT_ICON)  \
		$(PSP_EBOOT_ICON1) $(PSP_EBOOT_UNKPNG) $(PSP_EBOOT_PIC1)  \
		$(PSP_EBOOT_SND0) NULL $(PSP_EBOOT_PSAR)

SCEkxploit: $(TARGET).elf $(PSP_EBOOT_SFO)
	@echo Compiling SCEkxploit...
	@$(MKDIR) -p "__SCE__$(TARGET)"
	@$(STRIP) $(TARGET).elf -o __SCE__$(TARGET)/$(PSP_EBOOT)
	@$(MKDIR) -p "%__SCE__$(TARGET)"
	@$(PACK_PBP) "%__SCE__$(TARGET)/$(PSP_EBOOT)" $(PSP_EBOOT_SFO) $(PSP_EBOOT_ICON)  \
		$(PSP_EBOOT_ICON1) $(PSP_EBOOT_UNKPNG) $(PSP_EBOOT_PIC1)  \
		$(PSP_EBOOT_SND0) NULL $(PSP_EBOOT_PSAR)

$(TARGET).elf: $(OBJS) $(EXPORT_OBJ)
	@echo Linking $@
	@$(LINK.c) $^ $(LIBS) -o $@
	@$(FIXUP) $@

$(TARGET_BUILD).elf: $(OBJS) $(EXPORT_OBJ)
	@echo Linking $@
	@$(LINK.c) $^ $(LIBS) -o $@
	@$(FIXUP) $@

#Unknown what to do with this, stay as it was
$(TARGET_LIB): $(OBJS)
	@echo Ranlib $@
	@$(AR) cru $@ $(OBJS)
	@$(RANLIB) $@

$(PSP_EBOOT_SFO):
$(TARGET_EBOOT_SFO):
	@echo Creating SFO $@
ifndef BUILD_DIR
	@$(MKSFO) '$(PSP_EBOOT_TITLE)' $@
else
	@$(MKSFO) '$(PSP_EBOOT_TITLE)' $(TARGET_EBOOT_SFO)
endif

ifeq ($(BUILD_PRX),1)
$(PSP_EBOOT): $(TARGET).prx $(PSP_EBOOT_SFO)
	@echo Creating $@
	@$(PACK_PBP) $(PSP_EBOOT) $(PSP_EBOOT_SFO) $(PSP_EBOOT_ICON)  \
		$(PSP_EBOOT_ICON1) $(PSP_EBOOT_UNKPNG) $(PSP_EBOOT_PIC1)  \
		$(PSP_EBOOT_SND0)  $(TARGET).prx $(PSP_EBOOT_PSAR)
$(TARGET_EBOOT): $(TARGET_BUILD).prx $(TARGET_EBOOT_SFO)
	@echo Creating $@
	@$(PACK_PBP) $(TARGET_EBOOT) $(TARGET_EBOOT_SFO) $(PSP_EBOOT_ICON)  \
		$(PSP_EBOOT_ICON1) $(PSP_EBOOT_UNKPNG) $(PSP_EBOOT_PIC1)  \
		$(PSP_EBOOT_SND0)  $(TARGET_BUILD).prx $(PSP_EBOOT_PSAR)
else
#Custom EBOOT rule!
$(PSP_EBOOT): $(TARGET).elf $(PSP_EBOOT_SFO)
	@echo Creating $@
	@$(STRIP) $(TARGET).elf -o $(TARGET)_strip.elf
	@$(PACK_PBP) $(PSP_EBOOT) $(PSP_EBOOT_SFO) $(PSP_EBOOT_ICON)  \
		$(PSP_EBOOT_ICON1) $(PSP_EBOOT_UNKPNG) $(PSP_EBOOT_PIC1)  \
		$(PSP_EBOOT_SND0)  $(TARGET)_strip.elf $(PSP_EBOOT_PSAR)
	-$(RM) -f $(TARGET)_strip.elf

$(TARGET_EBOOT): $(TARGET_BUILD).elf $(TARGET_EBOOT_SFO)
	@echo Creating $@
	@$(STRIP) $(TARGET_BUILD).elf -o $(TARGET_BUILD)_strip.elf
	@$(PACK_PBP) $(TARGET_EBOOT) $(TARGET_EBOOT_SFO) $(PSP_EBOOT_ICON)  \
		$(PSP_EBOOT_ICON1) $(PSP_EBOOT_UNKPNG) $(PSP_EBOOT_PIC1)  \
		$(PSP_EBOOT_SND0)  $(TARGET_BUILD)_strip.elf $(PSP_EBOOT_PSAR)
	-$(RM) -f $(TARGET_BUILD)_strip.elf
endif

#Below is modified for different directories!
%.prx: %.elf
	@echo Creating prx $@
	@psp-prxgen $< $@
ifeq ($(ENCRYPT),1)
	@echo Encrypting prx $@
	@$(ENC) $@ $@
endif

#Unknown:
%.c: %.exp
	@echo Creating exports for $@
	@psp-build-exports -b $< > $@


#Dummy to prevent compiling .d.o invalid paths!
%.d:
	@

$(BUILD_DIR)/%.o:
	@echo Compiling $(subst ___/,../,$(patsubst $(BUILD_DIR)/%.o,%.c,$@))
	@$(CC) $(CFLAGS) -c -o $@ $(subst ___/,../,$(patsubst $(BUILD_DIR)/%.o,%.c,$@))

%.o: %.m
	@echo Compiling $(subst ___/,../,$(patsubst $(BUILD_DIR)/%.o,%.m,$@))
	@$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.mm
	@echo Compiling $(subst ___/,../,$(patsubst $(BUILD_DIR)/%.o,%.mm,$@))
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.d
	@$(GDC) $(DFLAGS) -c -o $@ $<

clean:
	@echo Cleaning up...
ifndef BUILD_DIR
	-$(RM) -f $(FINAL_TARGET) $(EXTRA_CLEAN) $(OBJS) $(patsubst %.o,%.d,$(OBJS)) $(PSP_EBOOT_SFO) $(PSP_EBOOT) $(EXTRA_TARGETS)
else
	-$(RM) -f $(FINAL_TARGET_BUILD) $(EXTRA_CLEAN) $(OBJS) $(patsubst %.o,%.d,$(OBJS)) $(PSP_EBOOT_SFO) $(PSP_EBOOT) $(EXTRA_TARGETS)
endif

rebuild: clean psp
