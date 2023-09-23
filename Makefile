WITH_ZYDIS      ?= n
WITH_LWIP       ?= y
WITH_TLSF       ?= n
WITH_MUSL       ?= n
WITH_NEWLIB     ?= n

UK_ROOT  ?= $(PWD)/workdir/unikraft
UK_LIBS  ?= $(PWD)/workdir/libs
UK_BUILD ?= $(PWD)/workdir/build
UK_PLATS ?= $(PWD)/workdir/plats

LIBS-y                  := $(UK_LIBS)/libelf
LIBS-$(WITH_ZYDIS)      := $(LIBS-y):$(UK_LIBS)/zydis
LIBS-$(WITH_LWIP)       := $(LIBS-y):$(UK_LIBS)/lwip
LIBS-$(WITH_TLSF)       := $(LIBS-y):$(UK_LIBS)/tlsf
LIBS-$(WITH_MUSL)       := $(LIBS-y):$(UK_LIBS)/musl
LIBS-$(WITH_NEWLIB)     := $(LIBS-y):$(UK_LIBS)/newlib
PLATS-y                 :=

all:
	@$(MAKE) -C $(UK_ROOT) A=$(PWD) L=$(LIBS-y) O=$(UK_BUILD) P=$(PLATS-y)

$(MAKECMDGOALS):
	@$(MAKE) -C $(UK_ROOT) A=$(PWD) L=$(LIBS-y) O=$(UK_BUILD) P=$(PLATS-y) $(MAKECMDGOALS)
