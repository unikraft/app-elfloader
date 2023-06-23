while IFS=' ' read -r vdso_symbol _; do
  objcopy --add-symbol $vdso_symbol=.text:$VDSO_MAGIC_NUMBER,global,function $APPELFLOADER_BASE/vdso/libvdso.o
  ((VDSO_MAGIC_NUMBER++))
done < $APPELFLOADER_BASE/vdso/vdso_mapping.conf
