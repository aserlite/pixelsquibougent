# VJ Engine

Petit moteur de VJing audio-réactif codé en C++17 et OpenGL.
Le projet tourne en temps réel, réagit au son et se pilote depuis le PC ou un smartphone.

## Fonctionnalités

- Analyse audio en direct sur fichier MP3 ou entrée micro.
- Rendu visuel sous OpenGL avec shaders GLSL modifiables en direct.
- Gestion d'images et de GIFs animés synchronisés sur le rythme.
- Interface de contrôle de bureau avec ImGui.
- Télécommande web accessible sur téléphone via le réseau local.
- Export vidéo hors-ligne en 60 FPS pour enregistrer des sets.

## Compilation

Il faut avoir CMake et un compilateur C++17 (GCC, Clang) et les librairies pour OpenGL et GLFW.

```bash
mkdir -p build && cd build
cmake ..
cmake --build . -j$(nproc)
```

## Utilisation

Pour lancer avec un fichier audio :

```bash
./build/bin/VJ assets/audio/test.mp3
```

Pour utiliser le micro directement, lance simplement sans argument :

```bash
./build/bin/VJ
```

L'interface web pour le téléphone est accessible sur http://localhost:18080 ou via l'adresse IP locale du PC.

## Structure

- src : Code source du moteur (audio, rendu, réseau, orchestrateur).
- shaders : Shaders GLSL rechargés automatiquement en cas de modification.
- web_client : Page web mobile pour le contrôle à distance.
- assets : Dossiers pour ranger les sons et les images/GIFs du deck.
