# System Szkoleniowy

## Opis Projektu
Projekt "System Szkoleniowy", jest systemem do prowadzenia szkoleń online. Projekt został przedstawiony 18.01.2024, a sprawozdanie zostało oddane 25.01.2024.

## Specyfikacja Projektu
- System obsługuje konta prowadzących oraz uczestników, skalując się od kilku do kilkuset użytkowników w jednym szkoleniu.
- Użytkownicy autentykują się za pomocą unikalnych nazw i haseł zapisanych w pliku `users.txt`.
- Komunikacja w systemie odbywa się w czasie rzeczywistym dzięki gniazdom nieblokującym, zapewniając wysoką wydajność, zwłaszcza przy dużej liczbie uczestników

## Architektura Systemu
1. **Serwer UDP** - Asynchroniczny i skalowalny, wykorzystuje funkcję `select` do obsługi wielu połączeń jednocześnie.
2. **Klient UDP** - Oddziela funkcje wysyłania i odbierania danych, posiada interaktywny interfejs użytkownika dla łatwej interakcji z systemem.
3. **Protokół Komunikacyjny** - Używa UDP dla szybkości i efektywności

## Pliki Systemu Szkoleniowego
- `client_func.h`: Plik nagłówkowy dla klienta, zawiera dyrektywy preprocesora, deklaracje bibliotek, stałe, i definicje struktur.
- `func.h`: Plik nagłówkowy dla serwera, zawiera dodatkowe definicje struktur i funkcji.
- `recv.c`: Plik źródłowy C dla serwera, odpowiada za odbieranie danych i przetwarzanie żądań klienta UDP.
- `send.c`: Plik źródłowy C dla klienta, odpowiada za wysyłanie danych do serwera

## Instalacja i Użycie
1. Sklonuj repozytorium na swoje urządzenie.
2. Upewnij się, że masz zainstalowane wymagane biblioteki i narzędzia do kompilacji kodu w C.
3.Nadaj uprawnienia plikowi install.sh
4. Skompiluj pliki źródłowe za pomocą kompilatora C lub za pomocą install.sh.
5. Uruchom serwer i klienta w oddzielnych terminalach.
6. Postępuj zgodnie z instrukcjami w interfejsie użytkownika klienta, aby zalogować się i korzystać z systemu.

## Autorzy
- Tomasz

