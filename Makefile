#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITXENON)),)
$(error "Please set DEVKITXENON in your environment. export DEVKITXENON=<path to>devkitPPC")
endif

include $(DEVKITXENON)/rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source source/main source/memory source/r4300 source/r4300/ppc \
			source/r4300/ppc/disasm source/rsp_hle-ppc source/xenos_audio \
			source/Rice_GX_Xenos
DATA		:=	  
INCLUDES	:=	files

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

MCHK = -Wl,-wrap,malloc  -Wl,-wrap,memalign -Wl,-wrap,realloc -Wl,-wrap,calloc -Wl,-wrap,free -DMCHK

OPTIFLAGS =  -O2 -mcpu=cell -mtune=cell -fno-tree-vectorize -fno-tree-slp-vectorize -ftree-vectorizer-verbose=1 -flto -fuse-linker-plugin 
#OPTIFLAGS =  -O2 -mcpu=cell -mtune=cell -fno-tree-vectorize -fno-tree-slp-vectorize -ftree-vectorizer-verbose=1

ASFLAGS	= -Wa,$(INCLUDE) -Wa,-a32
CFLAGS	= $(OPTIFLAGS) -g -Wall -Wno-format $(MACHDEP) $(INCLUDE) -DPPC_DYNAREC -D__BIG_ENDIAN__ -D_BIG_ENDIAN -DDEBUG_ -DUSE_RECOMP_CACHE -DUSE_EXPANSION -DFASTMEM -DNO_ASM
CXXFLAGS	=	$(CFLAGS)

#MACHDEP_LD =  -DXENON -m32 -maltivec -fno-pic -mhard-float -L$(DEVKITXENON)/xenon/lib/32
LDFLAGS	= $(MACHDEP) $(OPTIFLAGS) -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS	:=	-lxemit -lzlx -lpng  -lz -lfat -lext2fs -lntfs -lxtaf -lxenon -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= 

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
					$(sFILES:.s=.o) $(SFILES:.S=.o)

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBXENON_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					-L$(LIBXENON_LIB)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).elf32

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).elf32: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#source/ffs_content.c: genffs.py data/ps.psu data/vs.vsu
#	python genffs.py > source/ffs_content.c

run: $(BUILD) $(OUTPUT).elf32
	cp $(OUTPUT).elf32 /tftpboot/xenon
	$(PREFIX)strip /tftpboot/xenon
#	/home/dev360/run
