WITH_ZYDIS      ?= y

UK_ROOT  ?= $(PWD)/../../unikraft
UK_LIBS  ?= $(PWD)/../../libs
UK_PLATS ?= $(PWD)/../../plats

LIBS-y                  := $(UK_LIBS)/libelf
LIBS-$(WITH_ZYDIS)      := $(LIBS-y):$(UK_LIBS)/zydis
PLATS-y                 :=

all:
	@$(MAKE) -C $(UK_ROOT) A=$(PWD) L=$(LIBS-y) P=$(PLATS-y)

$(MAKECMDGOALS):
	@$(MAKE) -C $(UK_ROOT) A=$(PWD) L=$(LIBS-y) P=$(PLATS-y) $(MAKECMDGOALS)
