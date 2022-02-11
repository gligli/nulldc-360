#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITXENON)),)
$(error "Please set DEVKITXENON in your environment. export DEVKITXENON=<path to>devkitPPC")
endif

include $(DEVKITXENON)/rules

GUI_SRC		:=	gui gui/gui gui/images gui/sounds gui/fonts gui/lang gui/utils
GUI_INC		:=	gui
GUI_FLAGS	:=	-DUSE_GUI -DNO_SOUND

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	nullDC nullDC/dc nullDC/dc/aica nullDC/dc/asic nullDC/dc/gdrom nullDC/dc/maple nullDC/dc/mem nullDC/dc/pvr nullDC/dc/sh4 nullDC/plugins nullDC/config nullDC/cl nullDC/gui \
			nullDC/dc/sh4/rec_v1 nullDC/dc/sh4/shil nullDC/dc/sh4/shil/compiler nullDC/emitter nullDC/emitter/regalloc nullDC/emitter/disasm \
			plugins/xenon_gui \
			plugins/drkPvr \
			plugins/ImgReader plugins/ImgReader/deps \
			plugins/nullAICA \
			plugins/vbaARM \
			plugins/nullExtDev \
			plugins/XMaple \
			$(GUI_SRC)
DATA		:=	  
INCLUDES	:=	files nullDC . nullDC/dc/sh4 $(GUI_INC)

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

MCHK = -Wl,-wrap,malloc  -Wl,-wrap,memalign -Wl,-wrap,realloc -Wl,-wrap,calloc -Wl,-wrap,free -DMCHK

OPTIFLAGS = -Ofast -mcpu=cell -mtune=cell -fno-tree-vectorize -fno-tree-slp-vectorize -ftree-vectorizer-verbose=1 #-flto -fuse-linker-plugin 

ASFLAGS	= -Wa,$(INCLUDE) -Wa,-a32
CFLAGS	= $(OPTIFLAGS) $(GUI_FLAGS) -g -pipe -Wall -Wno-format -Wno-write-strings -Wno-strict-aliasing $(MACHDEP) $(INCLUDE) -D__POWERPC__
CXXFLAGS	=	$(CFLAGS)

MACHDEP_LD =  -DXENON -m32 -maltivec -fno-pic -mhard-float -L$(DEVKITXENON)/xenon/lib/32 -u read -u _start -u exc_base

LDFLAGS	= -g $(OPTIFLAGS) $(MACHDEP_LD) -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS	:=	-lpng -lbz2 -lfreetype -lbz2 -lz -lfat -lntfs -lxtaf -lext2fs -lxenon -lm

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
TTFFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.ttf)))
LANGFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.lang)))
PNGFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.png)))
PCMFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.pcm)))

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
					$(sFILES:.s=.o) $(SFILES:.S=.o) \
					$(TTFFILES:.ttf=.ttf.o) $(LANGFILES:.lang=.lang.o) \
					$(PNGFILES:.png=.png.o) \
					$(PCMFILES:.pcm=.pcm.o)

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBXENON_INC) -I$(LIBXENON_INC)/freetype2

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
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile -j4

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

#---------------------------------------------------------------------------------
# This rule links in binary data with these extensions: ttf lang png pcm
#---------------------------------------------------------------------------------
%.ttf.o : %.ttf
	@echo $(notdir $<)
	$(bin2o)
	
%.lang.o : %.lang
	@echo $(notdir $<)
	$(bin2o)

%.png.o : %.png
	@echo $(notdir $<)
	$(bin2o)
	
%.pcm.o : %.pcm
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#source/ffs_content.c: genffs.py data/ps.psu data/vs.vsu
#	python genffs.py > source/ffs_content.c

# ced config
run: $(BUILD) $(OUTPUT).elf32
	cp $(OUTPUT).elf32 /mnt/hgfs/x360dev/tftp/xenon.elf
	$(PREFIX)strip /mnt/hgfs/x360dev/tftp/xenon.elf
