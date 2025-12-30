#!/bin/bash
# run.sh
# Script per creare build, compilare con CMake e lanciare il progetto

# Esci se qualche comando fallisce
set -e

# Creazione cartella build
mkdir -p build
mkdir -p log

# Entra in build
cd build

# Genera makefile con CMake
cmake ..

# Compila tutto
make -j$(nproc)

echo "Build completata! Eseguibili in build/"

cd ..

# Opzionale: lancia main
./build/main 
