# Windows Dynamic Island

Ce projet vise à créer une application "Dynamic Island" pour Windows, inspirée de celle d'Apple, offrant des fonctionnalités de gestion de la musique, des notifications et d'autres informations importantes.

## Structure du Projet

```
WindowsDynamicIsland/
├── main.cpp
├── app.manifest
├── Core/
│   ├── WindowManager.h
│   └── WindowManager.cpp
├── Graphics/
└── Modules/
```

## Compilation et Exécution (Visual Studio Community)

1.  **Prérequis :**
    *   Visual Studio Community (ou une version supérieure) installé.
    *   Charge de travail "Développement de bureau en C++" installée.
    *   SDK Windows 10 ou 11.

2.  **Ouvrir le Projet :**
    *   Lancez Visual Studio.
    *   Sélectionnez "Fichier" > "Nouveau" > "Projet".
    *   Choisissez "Application de bureau Windows" (Windows Desktop Application) en C++.
    *   Nommez le projet `WindowsDynamicIsland` et placez-le dans le répertoire racine `WindowsDynamicIsland` que vous avez créé.
    *   Une fois le projet créé, supprimez les fichiers `.cpp` et `.h` générés par défaut (par exemple, `WindowsDynamicIsland.cpp`, `WindowsDynamicIsland.h`, `framework.h`, `Resource.h`, `targetver.h`, `stdafx.h`, `stdafx.cpp`).

3.  **Ajouter les Fichiers Existants :**
    *   Dans l'Explorateur de solutions de Visual Studio, faites un clic droit sur le projet `WindowsDynamicIsland`.
    *   Sélectionnez "Ajouter" > "Élément existant...".
    *   Naviguez vers le répertoire `WindowsDynamicIsland` et ajoutez `main.cpp` et `app.manifest`.
    *   Faites de même pour les fichiers `WindowManager.h` et `WindowManager.cpp` situés dans le dossier `Core`.

4.  **Configuration du Manifeste :**
    *   Assurez-vous que `app.manifest` est inclus dans le projet et que la propriété "Type de manifeste" est définie sur "Manifeste d'application" (ou similaire, selon la version de VS).
    *   Le fichier `app.manifest` est déjà configuré avec `uiAccess="true"` pour les privilèges nécessaires.

5.  **Compiler et Exécuter :**
    *   Sélectionnez la configuration "Release" et la plateforme "x64".
    *   Cliquez sur "Générer" > "Générer la solution" ou appuyez sur `F7`.
    *   Une fois la compilation réussie, vous pouvez exécuter l'application en appuyant sur `F5` (Démarrer le débogage) ou `Ctrl+F5` (Démarrer sans débogage).

    *Note : Pour que `uiAccess="true"` fonctionne, l'exécutable doit être signé numériquement et placé dans un répertoire sécurisé (par exemple, `Program Files`). Pour les tests, vous devrez peut-être désactiver temporairement l'UAC ou exécuter Visual Studio en tant qu'administrateur, bien que cela ne soit pas recommandé pour un usage quotidien.*

## Prochaines Étapes

1.  **Amélioration du Design (Graphics) :**
    *   Implémenter les animations d'expansion et de rétraction fluides.
    *   Affiner la zone cliquable de la Dynamic Island.
    *   Ajouter des bordures lumineuses (Glow) lors des notifications.

2.  **Gestion de la Musique (Modules/MediaManager) :**
    *   Le `MediaManager` est initialisé dans `main.cpp` et inclut un rappel pour les changements de média.
    *   L'implémentation actuelle utilise un stub pour `ISystemMediaTransportControls`.
    *   Les prochaines étapes incluront l'intégration réelle avec l'API `SystemMediaTransportControls` pour intercepter les événements musicaux, afficher la pochette d'album, le titre et l'artiste, et potentiellement intégrer les paroles de Spotify.

3.  **Notifications (Modules/NotificationManager) :**
    *   Le `NotificationManager` est initialisé dans `main.cpp` et inclut un rappel pour les notifications.
    *   L'implémentation actuelle utilise un stub pour `UserNotificationListener`.
    *   Les prochaines étapes incluront l'intégration réelle avec l'API `Windows.UI.Notifications.Management` pour intercepter les notifications et permettre la réponse rapide aux messages.

4.  **Écran de Verrouillage (Modules/LockScreenManager) :**
    *   Le `LockScreenManager` est initialisé dans `main.cpp` et inclut un rappel pour les changements d'état de l'écran de verrouillage.
    *   L'implémentation actuelle utilise un stub pour la gestion de l'écran de verrouillage.
    *   Les prochaines étapes incluront l'intégration réelle pour gérer l'affichage et les interactions sur l'écran de verrouillage, potentiellement via un `Credential Provider` ou des notifications de session.

5.  **Suivi des Applications :**
    *   Utiliser `UIAutomation` pour surveiller l'état d'autres applications (ex: Claude).

Je vous invite à suivre ces instructions pour configurer le projet dans Visual Studio. N'hésitez pas si vous avez des questions lors de la configuration ou si vous souhaitez que je commence à développer l'un des modules spécifiques.
