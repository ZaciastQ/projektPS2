#!/bin/bash

chmod 777 *

gcc -o send send.c client_func.h

if [ $? -eq 0 ]; then
    echo "Kompilacja send.c zakończona powodzeniem."
else
    echo "Błąd podczas kompilacji send.c"
    exit 1
fi


gcc -o recv recv.c func.h


if [ $? -eq 0 ]; then
    echo "Kompilacja recv.c zakończona powodzeniem."
else
    echo "Błąd podczas kompilacji recv.c"
    exit 1
fi

echo "Skrypt instalacyjny zakończony."
