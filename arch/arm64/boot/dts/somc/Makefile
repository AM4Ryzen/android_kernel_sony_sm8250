ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_MACH_SONY_EDO_CDB) += \
	kona-edo-cdb-overlay.dtbo
dtbo-$(CONFIG_MACH_SONY_PDX203) += \
	kona-edo-pdx203_generic-overlay.dtbo
dtbo-$(CONFIG_MACH_SONY_PDX203_J) += \
	kona-edo-pdx203_j-overlay.dtbo
dtbo-$(CONFIG_MACH_SONY_PDX204) += \
	kona-edo-pdx204-overlay.dtbo
dtbo-$(CONFIG_MACH_SONY_PDX206) += \
	kona-edo-pdx206_generic-overlay.dtbo
dtbo-$(CONFIG_MACH_SONY_PDX206_J) += \
	kona-edo-pdx206_j-overlay.dtbo
ifeq ($(CONFIG_MACH_SONY_EDO_CDB),y)
kona-edo-cdb-overlay.dtbo-base := ../qcom/kona.dtb ../qcom/kona-v2.dtb ../qcom/kona-v2.1.dtb
endif
ifeq ($(CONFIG_MACH_SONY_PDX203),y)
kona-edo-pdx203_generic-overlay.dtbo-base := ../qcom/kona.dtb ../qcom/kona-v2.dtb ../qcom/kona-v2.1.dtb
endif
ifeq ($(CONFIG_MACH_SONY_PDX203_J),y)
kona-edo-pdx203_j-overlay.dtbo-base := ../qcom/kona.dtb ../qcom/kona-v2.dtb ../qcom/kona-v2.1.dtb
endif
ifeq ($(CONFIG_MACH_SONY_PDX204),y)
kona-edo-pdx204-overlay.dtbo-base := ../qcom/kona.dtb ../qcom/kona-v2.dtb ../qcom/kona-v2.1.dtb
endif
ifeq ($(CONFIG_MACH_SONY_PDX206),y)
kona-edo-pdx206_generic-overlay.dtbo-base := ../qcom/kona.dtb ../qcom/kona-v2.dtb ../qcom/kona-v2.1.dtb
endif
ifeq ($(CONFIG_MACH_SONY_PDX206_J),y)
kona-edo-pdx206_j-overlay.dtbo-base := ../qcom/kona.dtb ../qcom/kona-v2.dtb ../qcom/kona-v2.1.dtb
endif

else
dtb-$(CONFIG_MACH_SONY_EDO_CDB) += \
	kona-edo-cdb.dtb \
	kona-v2-edo-cdb.dtb \
	kona-v2.1-edo-cdb.dtb
dtb-$(CONFIG_MACH_SONY_PDX203) += \
	kona-edo-pdx203_generic.dtb \
	kona-v2-edo-pdx203_generic.dtb \
	kona-v2.1-edo-pdx203_generic.dtb
dtb-$(CONFIG_MACH_SONY_PDX203_J) += \
	kona-edo-pdx203_j.dtb \
	kona-v2-edo-pdx203_j.dtb \
	kona-v2.1-edo-pdx203_j.dtb
dtb-$(CONFIG_MACH_SONY_PDX204) += \
	kona-edo-pdx204.dtb \
	kona-v2-edo-pdx204.dtb \
	kona-v2.1-edo-pdx204.dtb
dtb-$(CONFIG_MACH_SONY_PDX206) += \
	kona-edo-pdx206_generic.dtb \
	kona-v2-edo-pdx206_generic.dtb \
	kona-v2.1-edo-pdx206_generic.dtb
dtb-$(CONFIG_MACH_SONY_PDX206_J) += \
	kona-edo-pdx206_j.dtb \
	kona-v2-edo-pdx206_j.dtb \
	kona-v2.1-edo-pdx206_j.dtb
endif

ifeq ($(CONFIG_ARM64),y)
always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
else
targets += dtbs
targets += $(addprefix ../, $(dtb-y))

$(obj)/../%.dtb: $(src)/%.dts FORCE
	$(call if_changed_dep,dtc)

dtbs: $(addprefix $(obj)/../,$(dtb-y))
endif
clean-files	:= *.dtb *.dtbo
