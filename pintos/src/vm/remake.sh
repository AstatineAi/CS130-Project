#!/bin/fish

make clean
bear -- make
mv compile_commands.json build/
cd build/
pintos-mkdisk filesys.dsk --filesys-size=2
pintos-mkdisk swap.dsk --swap-size=4
cd ..

echo "vm/ clean & rebuild."
