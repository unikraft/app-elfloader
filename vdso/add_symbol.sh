#!/bin/bash
OBJCOPY_CMD=${OBJCOPY_CMD:-objcopy}
CONF_FILE=${1}
IN_FILE=${2}
OUT_FILE=${3}

cp -f "${IN_FILE}" "${OUT_FILE}"
while IFS=' ' read -r vdso_symbol _; do
	"${OBJCOPY_CMD}" --add-symbol "${vdso_symbol}=.text:${VDSO_MAGIC_NUMBER},global,function" "${OUT_FILE}"
	((VDSO_MAGIC_NUMBER++))
done < "${CONF_FILE}"
