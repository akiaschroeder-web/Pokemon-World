# Pokémon World: the web build (make PLATFORM=web web).
#
# The same game code, compiled to WebAssembly with Emscripten instead of for the GBA.
# - C files: preprocessed and run through preproc as usual, then compiled with emcc.
# - Data in GNU assembler (maps, scripts, battle scripts, songs): assembled with the
#   host's GNU as in 32-bit mode, then converted to WebAssembly assembly by
#   tools/web/elf2wasm.py, which keeps every symbol and turns each address back into
#   a relocation.
# - src/*.s (GBA startup code, the sound mixer, decompression) is replaced by C
#   under src/platform/.
# The result goes to build/web/site/ (index.html, pokeworld.js, pokeworld.wasm).

WEB_CC ?= emcc
WEB_HOST_AS ?= as
WEB_OPT ?= -O2

WEB_DEFS := -DPORTABLE -DPLATFORM_WEB -DNONMATCHING -DUBFIX
WEB_CFLAGS := $(WEB_OPT) -std=gnu17 $(WEB_DEFS) \
	-Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-unknown-attributes -Wno-ignored-attributes \
	-Wno-unused-command-line-argument -Wno-gnu-designator -Wno-initializer-overrides \
	-Wno-unknown-pragmas -Wno-pointer-sign -Wno-incompatible-pointer-types-discards-qualifiers \
	-Wno-constant-conversion -Wno-tautological-constant-out-of-range-compare -Wno-array-bounds \
	-Wno-builtin-declaration-mismatch -Wno-incompatible-library-redeclaration \
	-Wno-error=incompatible-function-pointer-types -fno-strict-aliasing -fwrapv -funsigned-char
WEB_ASFLAGS := --32 --defsym MODERN=1 --defsym $(GAME_VERSION)=1 --defsym PORTABLE=1

WEB_SITE := $(OBJ_DIR)/site
WEB_FUNCTYPES := $(OBJ_DIR)/functypes.txt

# GBA-only C files with no meaning in the browser.
WEB_C_EXCLUDE := $(C_SUBDIR)/libisagbprn.c $(C_SUBDIR)/librfu_intr.c $(C_SUBDIR)/librfu_rfu.c \
	$(C_SUBDIR)/librfu_stwi.c $(C_SUBDIR)/librfu_sio32id.c $(C_SUBDIR)/rom_header_gf.c \
	$(C_SUBDIR)/rom_header_rhh.c $(C_SUBDIR)/agb_flash_le.c $(C_SUBDIR)/agb_flash_1m.c \
	$(C_SUBDIR)/agb_flash_mx.c $(C_SUBDIR)/agb_flash.c $(C_SUBDIR)/siirtc.c
WEB_C_SRCS := $(filter-out $(WEB_C_EXCLUDE),$(C_SRCS))
WEB_C_OBJS := $(patsubst $(C_SUBDIR)/%.c,$(C_BUILDDIR)/%.o,$(WEB_C_SRCS))
WEB_DATA_OBJS := $(DATA_ASM_OBJS) $(MID_OBJS)
WEB_OBJS := $(WEB_C_OBJS) $(WEB_DATA_OBJS)

$(shell mkdir -p $(sort $(dir $(WEB_OBJS))) $(WEB_SITE))

# --- C
$(C_BUILDDIR)/%.o: $(C_SUBDIR)/%.c
	@echo "emcc <flags> -o $@ $<"
	@$(WEB_CC) -E -x c $(CPPFLAGS) $(WEB_DEFS) $< | $(PREPROC) -i -g $(ASSETS_DIR_NAME) $< charmap.txt | $(WEB_CC) $(WEB_CFLAGS) -x c -c - -o $@

$(C_BUILDDIR)/%.d: $(C_SUBDIR)/%.c
	$(SCANINC) -M $@ -g $(ASSETS_DIR_NAME) $(INCLUDE_SCANINC_ARGS) -I tools/agbcc/include $<

ifneq ($(NODEP),1)
-include $(addprefix $(OBJ_DIR)/,$(WEB_C_SRCS:.c=.d))
endif

# --- data: GNU as (32-bit) -> ELF -> WebAssembly assembly -> object
$(WEB_FUNCTYPES): $(WEB_C_OBJS)
	python3 tools/web/wasm_functypes.py $@ $(WEB_C_OBJS)

$(DATA_ASM_BUILDDIR)/%.elf.o: $(DATA_ASM_SUBDIR)/%.s
	$(PREPROC) -s $< charmap.txt | $(CPP) $(CPPFLAGS) -DPORTABLE $(INCLUDE_SCANINC_ARGS) - | $(PREPROC) -ie $< charmap.txt | $(WEB_HOST_AS) $(WEB_ASFLAGS) -o $@

$(DATA_ASM_BUILDDIR)/%.d: $(DATA_ASM_SUBDIR)/%.s
	$(SCANINC) -M $@ -g $(ASSETS_DIR_NAME) $(INCLUDE_SCANINC_ARGS) -I "" $<

ifneq ($(NODEP),1)
-include $(addprefix $(OBJ_DIR)/,$(DATA_ASM_SRCS:.s=.d))
endif

$(MID_BUILDDIR)/%.elf.o: $(MID_ASM_DIR)/%.s
	$(WEB_HOST_AS) $(WEB_ASFLAGS) -I sound -o $@ $<

%.wasm.s: %.elf.o $(WEB_FUNCTYPES) tools/web/elf2wasm.py
	python3 tools/web/elf2wasm.py $< $@ --functypes $(WEB_FUNCTYPES)

$(DATA_ASM_BUILDDIR)/%.o: $(DATA_ASM_BUILDDIR)/%.wasm.s
	$(WEB_CC) -c $< -o $@

$(MID_BUILDDIR)/%.o: $(MID_BUILDDIR)/%.wasm.s
	$(WEB_CC) -c $< -o $@

# --- link
WEB_LDFLAGS := $(WEB_OPT) -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sALLOW_MEMORY_GROWTH=1 \
	-sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sENVIRONMENT=web \
	-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU16 -sEXPORTED_FUNCTIONS=_main,_Web_GetFramebuffer,_Web_SetKeys,_Web_GetAudio \
	-sMODULARIZE=0 -lidbfs.js --shell-file platform/web/shell.html

web: $(WEB_SITE)/index.html

$(WEB_SITE)/index.html: $(WEB_OBJS) platform/web/shell.html platform/web/pokeworld.js
	$(WEB_CC) $(WEB_LDFLAGS) --pre-js platform/web/pokeworld.js $(WEB_OBJS) -o $@
	@echo "Web build: $(WEB_SITE)/"

# Compile everything but don't link (for checking without Emscripten's linker).
web-objs: $(WEB_OBJS)

.PHONY: web web-objs
