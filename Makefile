#
# Makefile
# 编译产物统一到 BUILD_DIR（默认 lvgl_build_output/）
#
CC ?= gcc
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= ${shell pwd}
CFLAGS ?= -O3 -g0 -I$(LVGL_DIR)/ -Wall -Wshadow -Wundef -Wmissing-prototypes -Wno-discarded-qualifiers -Wall -Wextra -Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith -fno-strict-aliasing -Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-ignored-qualifiers -Wno-error=pedantic -Wno-sign-compare -Wno-error=missing-prototypes -Wdouble-promotion -Wclobbered -Wdeprecated -Wempty-body -Wtype-limits -Wshift-negative-value -Wstack-usage=2048 -Wno-unused-value -Wno-unused-parameter -Wno-missing-field-initializers -Wuninitialized -Wmaybe-uninitialized -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wpointer-arith -Wno-cast-qual -Wmissing-prototypes -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wno-discarded-qualifiers -Wformat-security -Wno-ignored-qualifiers -Wno-sign-compare
LDFLAGS ?= -lm
BUILD_DIR ?= lvgl_build_output
BIN = $(BUILD_DIR)/demo


#Collect the files to compile
MAINSRC = ./main.c

include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

CSRCS +=$(LVGL_DIR)/mouse_cursor_icon.c 

OBJEXT ?= .o

# 源文件 → BUILD_DIR 下对应 .o（绝对路径去掉 LVGL_DIR/；仅文件名则平铺到 BUILD_DIR）
src_to_obj = $(BUILD_DIR)/$(if $(filter $(LVGL_DIR)/%,$(1)),$(patsubst $(LVGL_DIR)/%,%,$(1)),$(notdir $(1)))

AOBJS = $(foreach s,$(ASRCS),$(call src_to_obj,$(s:.S=$(OBJEXT))))
COBJS = $(foreach s,$(CSRCS),$(call src_to_obj,$(s:.c=$(OBJEXT))))
MAINOBJ = $(BUILD_DIR)/main.o

SRCS = $(ASRCS) $(CSRCS) $(MAINSRC)
OBJS = $(AOBJS) $(COBJS)

## MAINOBJ -> OBJFILES

all: default

# 为每个源生成显式规则（兼容 VPATH 下的仅文件名 CSRCS）
define compile_c_rule
$(2): $(1)
	@mkdir -p $$(dir $$@)
	@$$(CC) $$(CFLAGS) -c $$< -o $$@
	@echo "CC $$<"
endef

define compile_s_rule
$(2): $(1)
	@mkdir -p $$(dir $$@)
	@$$(CC) $$(CFLAGS) -c $$< -o $$@
	@echo "AS $$<"
endef

$(foreach s,$(CSRCS),$(eval $(call compile_c_rule,$(s),$(call src_to_obj,$(s:.c=$(OBJEXT))))))
$(foreach s,$(ASRCS),$(eval $(call compile_s_rule,$(s),$(call src_to_obj,$(s:.S=$(OBJEXT))))))

$(MAINOBJ): $(MAINSRC)
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

default: $(AOBJS) $(COBJS) $(MAINOBJ)
	@mkdir -p $(dir $(BIN))
	$(CC) -o $(BIN) $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
	rm -f demo
	# 清理历史散落在源码旁 / 工程根目录的 .o
	-find . -name '*.o' -type f ! -path '*/.git/*' -delete
