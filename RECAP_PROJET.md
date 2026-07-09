# UltraIsland (Windows Dynamic Island) — Récapitulatif du projet

> Version actuelle : **2.9.0.0** · App Win32 / Direct2D / C++20 · Windows 10/11 x64

## En une phrase

Une « Dynamic Island » façon Apple pour Windows : une pilule flottante toujours au premier plan, en haut de l'écran, qui affiche l'heure au repos et s'agrandit pour la musique, les notifications et les réglages système — avec un mode ambiant plein écran façon écran de verrouillage musical.

---

## 1. Architecture générale

```
main.cpp                    ← point d'entrée, orchestration des modules
Core/
  WindowManager.{h,cpp}      ← contrôleur central de la pilule (état, input, timers)
  AnimationEngine.{h,cpp}    ← interpolation des tailles/formes entre états
  IslandDim.h                ← dimensions & PillRT (paramètres runtime pilotés par le Cockpit)
  AppConfig.{h,cpp}          ← configuration persistante (JSON dans %LOCALAPPDATA%)
  CockpitWindow.{h,cpp}      ← app de réglages (remplace la fenêtre de 1er lancement)
  SettingsWindow.{h,cpp}     ← assistant de 1er lancement uniquement
  AmbientWindow.{h,cpp}      ← mode plein écran (horloge + lyrics + lecteur)
  TrayIcon.{h,cpp}           ← icône & menu dans la zone de notification
  Sounds.{h,cpp}             ← sons d'interface synthétisés (aucun fichier audio)
Graphics/
  Renderer.{h,cpp}           ← tout le dessin Direct2D de la pilule (1000+ lignes)
  GlassSurface.{h,cpp}       ← pipeline DirectComposition (fenêtre translucide)
  AnimationEngine            ← (voir Core)
  CompositionManager         ← code legacy, non branché (tentative acrylique abandonnée)
Modules/
  MediaManager.{h,cpp}       ← lecture en cours via SMTC (Spotify, VLC, navigateurs...)
  SpotifyClient.{h,cpp}      ← OAuth PKCE + file d'attente réelle (Spotify Web API)
  LyricsClient.{h,cpp}       ← paroles synchronisées (API lrclib.net, gratuite)
  NotificationManager.{h,cpp}← notifications Windows réelles (nécessite le paquet MSIX)
  LockScreenManager.{h,cpp}  ← détection verrouillage/déverrouillage de session
  SystemMonitor.{h,cpp}      ← CPU/RAM/réseau/batterie (thread de fond, 1 s)
packaging/
  AppxManifest.xml           ← manifeste du paquet MSIX
  build_msix.ps1             ← script de packaging/installation/désinstallation
```

### Le fil qui relie tout

`main.cpp` lit la config (`AppConfig`), instancie les modules activés, les connecte au `WindowManager` via des callbacks thread-safe, puis lance la boucle de messages Win32 standard. Le `WindowManager` est le chef d'orchestre : il possède la fenêtre, le `Renderer`, l'`AnimationEngine`, gère tous les clics/survols/molette, et pousse le contenu à afficher (`IslandContent`) au `Renderer` à chaque frame.

---

## 2. La fenêtre de la pilule — comment elle existe visuellement

- **Toujours au premier plan**, sans bordure, sans barre de titre (`WS_POPUP`), positionnée en haut de l'écran.
- **Transparente et cliquable seulement sur sa forme** : la fenêtre Win32 est rectangulaire, mais `SetWindowRgn` découpe une région qui suit la pilule, et le rendu (Direct2D + DirectComposition) ne peint que la forme arrondie — le reste est invisible et laisse passer les clics vers les fenêtres en dessous.
- **DirectComposition** (`GlassSurface`) fournit un vrai canal alpha par pixel : la pilule peut être semi-transparente et laisser deviner ce qu'il y a derrière (effet « verre »), sans dépendre de l'API d'acrylique Windows (abandonnée — trop instable/privée).
- **DPI-aware Per-Monitor V2** : toutes les tailles sont définies en pixels *logiques* puis converties en pixels *physiques* au moment de positionner/dessiner la fenêtre, pour rester nette sur les écrans à 125 %/150 % de mise à l'échelle.

## 3. Les états de la pilule (machine à états)

Gérés par `AnimationEngine` + `IslandState` (dans `IslandDim.h`). La pilule anime en douceur (courbes d'assouplissement) entre :

| État | Déclenché par |
|---|---|
| **Idle** | Repos — affiche l'heure, micro-icônes (batterie, etc.) |
| **MusicExpanded** | Survol de la pilule quand une musique joue |
| **MusicQueue** | Clic sur l'icône « file d'attente » dans la vue musique |
| **NotifExpanded / NotifList** | Réception d'une notification / historique |
| **HUDVolume / HUDBrightness / HUDNetwork** | Changement système (volume, luminosité...) |
| **SystemExpanded** | Clic sur la pilule pour ouvrir les contrôles rapides (Wi-Fi, Bluetooth, mode sombre, éclairage nocturne, sliders volume/luminosité) |
| **WifiList** | Clic sur le module Wi-Fi des contrôles système |

Toutes les tailles/formes de l'état **Idle** sont pilotées à chaud par le Cockpit (`PillRT` dans `IslandDim.h`) : largeur, hauteur, rayon, position (gauche/centre/droite), opacité, teinte, police.

## 4. Musique — comment ça marche

- **Source** : l'API Windows **SMTC** (`GlobalSystemMediaTransportControlsSessionManager`) via `MediaManager`. Elle capte **n'importe quel lecteur** qui expose ses métadonnées à Windows (Spotify, VLC, navigateurs, etc.) — pas seulement Spotify.
- **Sélection intelligente de session** : au lieu de suivre bêtement la « session courante » de Windows (qui reste parfois bloquée sur une app en pause), `MediaManager::PickBestSession()` choisit activement la session **réellement en train de jouer**.
- **Filet de sécurité anti-blocage** : toutes les ~2 s, `WindowManager` force une resynchronisation (`RequestRefresh`) pour ne jamais rester figé sur un ancien titre si un événement SMTC a été manqué.
- **Pochette adaptative** : la couleur/texture de fond de la pilule peut suivre la pochette de l'album (floutée), au lieu d'une couleur fixe — réglable dans le Cockpit.
- **Barre de progression interactive** : clic ou glissé pour avancer/reculer dans le morceau (`SeekTo`, via SMTC).
- **File d'attente réelle (« Playing Next »)** — **uniquement si Spotify est connecté** (voir section 5). Sans connexion, la pilule ne prétend pas connaître la suite : elle invite à se connecter. La liste est défilante (3 lignes visibles, molette pour le reste) ; cliquer sur un titre l'enchaîne (Spotify n'autorise pas de « sauter directement » à un index, donc l'app enchaîne les « suivant » nécessaires).

## 5. Spotify — intégration OAuth complète

`Modules/SpotifyClient.{h,cpp}` implémente un vrai client Spotify, sans dépendance externe :

- **Authorization Code + PKCE** (le flux recommandé pour une app native, sans secret client).
- Ouvre le navigateur par défaut pour l'autorisation, récupère le code via un petit serveur local temporaire.
- **Jeton de rafraîchissement stocké chiffré (DPAPI)** dans `%LOCALAPPDATA%\Ultraisland\`.
- Interroge `GET /me/player/queue` pour la vraie file d'attente, avec un parseur JSON manuel robuste (attention : Spotify place `album.artists[]` *avant* le nom du morceau dans sa réponse — piège classique évité).
- Reconnexion silencieuse au démarrage si un jeton valide existe déjà.
- Le Cockpit affiche l'état réel (connecté / non connecté) et propose Connecter/Déconnecter.

## 6. Notifications — ce qui marche et ce qui ne marche pas encore

- **Notifications système réelles** : nécessitent l'API `UserNotificationListener`, qui **exige que l'app soit installée en paquet MSIX** (une app .exe "nue" ne peut pas y accéder — restriction Windows, pas un bug). C'est pour ça qu'un packaging MSIX complet a été mis en place (voir section 9).
- **Notification affichée dans la pilule** : toast avec icône, app, titre, message ; se déclenche avec un léger effet « cloche qui tremble » ; **disparaît automatiquement après 2 secondes**, même si le curseur est dessus (un bug de calcul DPI empêchait ça avant d'être corrigé).
- **Historique des notifications** consultable dans la pilule (état `NotifList`).

## 7. Contrôles système rapides

Accessibles en cliquant la pilule en dehors de la musique (état `SystemExpanded`) :

- **Volume** et **luminosité** : sliders qui pilotent directement le système (API `IAudioEndpointVolume`, luminosité moniteur/WMI).
- **Wi-Fi** : bascule + liste des réseaux disponibles.
- **Bluetooth** : bascule via l'API `Windows.Devices.Radios`.
- **Mode sombre** : bascule le thème Windows.
- **Éclairage nocturne** : bascule via écriture directe du registre (Windows n'expose aucune API publique pour ça — le blob binaire de configuration est modifié manuellement).
- **Batterie** : pourcentage + état de charge (bug corrigé : la détection « en charge » était inversée).

## 8. Mode Ambiant — l'écran plein format

Ouvert par raccourci **Ctrl+Alt+L**, le menu du tray, ou **automatiquement** :
- après N minutes d'inactivité (configurable, off par défaut à 0),
- au déverrouillage de session si une musique est en cours.

Contenu (`AmbientWindow`) :
- **Fond** : pochette de l'album, fortement floutée, plein écran.
- **Paroles synchronisées** (`LyricsClient`, via l'API gratuite **lrclib.net**) : défilement centré façon Spotify — la ligne active grossit légèrement, fondu progressif des lignes voisines, **anticipation de 300 ms** sur le timing pour que le mot soit affiché pile au bon moment, courbe d'accélération/décélération douce (aucune coupure brutale entre les lignes).
- **Carte de contrôle unique**, effet **verre acrylique translucide uniforme** (pas de pochette dupliquée dedans) : titre, artiste, barre de progression cliquable, boutons précédent/lecture/suivant, **slider de volume**.
- Fermeture : touche Échap ou bouton ✕. L'îlot normal se ré-affiche à la fermeture.

## 9. Packaging MSIX

Une app Win32 classique est très limitée sur Windows moderne (pas de vraies notifications, pas d'accès à l'API d'écran de verrouillage). Le dossier `packaging/` fournit un vrai paquet MSIX :

- `AppxManifest.xml` déclare l'identité de l'app et la capacité `userNotificationListener`.
- `build_msix.ps1` automatise : build Release → génération des logos → `makeappx` → signature avec un certificat de test auto-généré → installation locale.
- Une fois installé via MSIX (`.\build_msix.ps1 -Install` en PowerShell **administrateur**), l'app obtient une véritable identité de package, ce qui débloque :
  - les vraies notifications Windows dans la pilule,
  - la possibilité de personnaliser le fond de l'**écran de verrouillage Windows** avec la pochette de l'album en cours (composée nette via GDI+, positionnée juste au-dessus de la zone habituelle des contrôles multimédias de Windows).
- Le script gère aussi la mise à jour (désinstalle l'ancienne version si conflit) et la désinstallation (`-Uninstall`).

## 10. Le Cockpit — centre de configuration

`CockpitWindow` est une mini-application de réglages en Direct2D (pas de contrôles Win32 classiques), ouverte depuis le menu du tray. Onglets :

- **Général** : activer/désactiver la pilule, démarrage avec Windows, lancement en administrateur.
- **Apparence** : forme de la pilule (pilule/cercle/carré arrondi...), taille, position à l'écran, couleur/teinte (fixe ou adaptative à la pochette), police et graisse du texte.
- **Sons** : active/désactive et choisit une variante pour 3 catégories de sons synthétisés (ouverture de la pilule, notification, connexion d'un appareil) — chaque son est généré par calcul (onde sinusoïdale + enveloppe), aucun fichier `.wav` dans le projet.
- **Modules** : quels modules sont actifs (musique, notifications, système, verrouillage).
- **Comptes** : état de connexion Spotify.

Toute la configuration est sauvegardée dans `%LOCALAPPDATA%\Ultraisland\config.json` et rechargée à chaud (pas besoin de redémarrer l'app pour la plupart des changements).

## 11. Threading — les règles à respecter

Plusieurs sources émettent des événements sur des **threads différents** du thread principal (UI) :
- Les callbacks SMTC/WinRT (musique, notifications) arrivent sur des threads d'arrière-plan WinRT.
- `SystemMonitor` tourne sur son propre thread (sondage every 1s).
- `SpotifyClient` fait ses appels réseau sur des threads dédiés.

**Règle stricte** : ces threads ne touchent jamais directement `WindowManager`/`Renderer`. Ils passent par des méthodes `Post*` qui empilent l'événement dans une file protégée par mutex, puis réveillent le thread UI via `PostMessage`. Le thread UI vide la file et applique les changements en toute sécurité. C'est ce qui a permis de corriger plusieurs blocages/plantages liés à des appels croisés entre threads.

## 12. Historique des correctifs marquants

- **Crash de connexion Spotify** : le flux OAuth bloquait le thread principal jusqu'à 90 s ; déplacé sur un thread dédié.
- **Batterie « faible » alors qu'en charge** : bit de détection de charge mal interprété dans l'API Windows — logique inversée corrigée.
- **Pilule bloquée en survol/notification** : incohérence entre pixels physiques et logiques dans le calcul DPI de sortie de survol — corrigée avec conversion systématique + un filet de sécurité qui force la fermeture après un délai, quoi qu'il arrive.
- **Fausse liste de « prochains titres »** sans connexion Spotify — supprimée, remplacée par une invitation claire à se connecter.
- **Transitions saccadées** (paroles, ouverture/fermeture) — remplacées par des courbes d'assouplissement, du fondu croisé et une anticipation de timing.

---

## Pour aller plus loin

- Un skill dédié **`ultraisland-expert`** existe dans `.claude/skills/` et `.agents/skills/` : il contient une cartographie fine du code, les pièges connus, et l'historique détaillé de chaque session de correctifs — à consulter avant toute modification profonde du projet.
- Build : ouvrir `WindowsDynamicIsland.slnx` dans Visual Studio, ou compiler en ligne de commande avec `msbuild WindowsDynamicIsland.vcxproj /p:Configuration=Release /p:Platform=x64`.
- Packaging MSIX : voir `packaging/build_msix.ps1`.
