MAINTAINERCLEANFILES = Makefile.in

# x86_64 only
SRC = @srcdir@/x86
ABSSRC = @abs_srcdir@/x86
ASM_H  = $(SRC)/7zAsm.asm
ASM_OPT += -I $(SRC)/
## For LZMA Assembler Decompressor
ASM_De = LzmaDecOpt
ASM_F  = LzFindOpt
ASM_S  = $(SRC)/$(ASM_De).asm $(SRC)/$(ASM_F).asm

noinst_LTLIBRARIES = liblzmaASM.la
liblzmaASM_la_SOURCES = $(ASM_S) $(ASM_H)

## lzmadecompress and lzfind
liblzmaASM_la_LIBADD = $(ASM_De).lo $(ASM_F).lo

LzmaDecOptFILE := \
\# $(ASM_De).lo - a libtool object file\
\n\# Generated by libtool -- hack to allow asm linking\
\n\# Peter Hyman\
\npic_object='.libs/$(ASM_De).o'\
\nnon_pic_object='$(ASM_De).o'

$(ASM_De).lo: $(SRC)/$(ASM_De).asm
	$(ASM_PROG) $(ASM_OPT) -o $(ASM_De).o $(ABSSRC)/$(ASM_De).asm
	@echo -e "$(LzmaDecOptFILE)" > $(ASM_De).lo
	@$(MKDIR_P) .libs
	@cp $(ASM_De).o .libs

LzFindOptFILE := \
\# $(ASM_F).lo - a libtool object file\
\n\# Generated by libtool -- hack to allow asm linking\
\n\# Peter Hyman\
\npic_object='.libs/$(ASM_F).o'\
\nnon_pic_object='$(ASM_F).o'

$(ASM_F).lo: $(SRC)/$(ASM_F).asm
	$(ASM_PROG) $(ASM_OPT) -o $(ASM_F).o $(ABSSRC)/$(ASM_F).asm
	@echo -e "$(LzFindOptFILE)" > $(ASM_F).lo
	@$(MKDIR_P) .libs
	@cp $(ASM_F).o .libs
