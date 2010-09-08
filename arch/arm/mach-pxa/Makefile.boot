   zreladdr-y				:=	0xa0008000
ifeq ($(CONFIG_ARCH_PXA),y)
   zreladdr-$(CONFIG_MACH_LOOX720)	:=	$(CONFIG_DRAM_BASE)+0x8000
endif
