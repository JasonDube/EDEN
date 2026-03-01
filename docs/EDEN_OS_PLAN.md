# EDEN OS — 3D Linux Distribution Plan

## Vision
A 3D operating system built on Linux where the filesystem IS the world. Folders are rooms you walk into, files are physical objects you interact with, and AI agents navigate the space alongside you. The terrain editor is the system configurator; play mode is the actual OS.

## Architecture

### Two Modes
- **Editor Mode** (terrain editor) = System configuration. Design your environment — terrain, lighting, object placement, terminal screens, etc.
- **Play Mode** (F5 / boot) = The OS itself. Walk around, interact with files, use terminals, talk to AI companions.

### Boot Flow
```
Login Screen → "EDEN OS" session → Load user config (~/.eden/desktop.eden)
  → If no config exists, load default world
  → Auto-enter play mode
  → Terminal + Claude auto-launched
```

### The World = Home Directory
- User spawns in `~/` — a large open area
- Each folder in `~/` is a room/building/structure the user can walk into
- Walking through a door = `cd` into that folder
- Walking back out = `cd ..`
- Standard folders become default structures:
  - `~/Documents/` → office/library room
  - `~/Downloads/` → storage room
  - `~/Pictures/` → gallery corridor with framed images on walls
  - `~/Music/` → listening room
  - `~/Videos/` → theater/screening room
  - `.config/`, `.local/` → hidden by default, underground/hidden areas

### Files as Physical Objects
- **Images** → thin plate/frame (art gallery style), texture mapped with the actual image
- **Videos** → screen objects that play on approach
- **Text files** → documents/scrolls, content rendered on surface
- **Executables** → interactive objects (buttons, levers, machines)
- **Folders** → doors/portals leading to sub-rooms
- **Unknown files** → generic cubes with filename label

### Permission System as Physical Security
- **Read permission** → you can see inside the room / see the object
- **Write permission** → you can move, rename, modify objects
- **Execute permission** → you can activate/run things
- **No permission** → robot guard blocks the door, demands password
- **sudo** → convince the guard with your password
- **Root-owned dirs** (`/etc/`, `/root/`) → fortress/checkpoint areas with armed guards
- **`~/.ssh/`** → vault with guard demanding passphrase
- **`/tmp/`** → garbage dump, janitor bots periodically clean it
- **`/dev/null`** → black hole you can throw things into
- **Hidden dotfiles** → invisible until "show hidden" toggle

### AI Agents in the Filesystem
- Send an agent into a folder: "Go find duplicate photos in Pictures"
- Agent walks through the gallery, examines each frame
- Reports back: "Found 47 duplicates, want me to move them?"
- Agents can sort, organize, search, clean up on your behalf
- Visual — you watch them working in the corridors

---

## Phases

### Phase 1: Folder Room Generation
**Goal:** Walk into a real filesystem folder and see its contents as 3D objects.

- Scan a directory and generate objects for each entry
  - Folders → door/portal objects with folder name
  - Files → cubes/plates with filename rendered on texture
- Entering a door scans that subdirectory and generates a new room
- Exiting goes back to parent
- Start with `~/` as the spawn area
- Room layout algorithm (grid, gallery wall, etc.)
- Performance: only generate objects for the current directory + immediate children doors
- File type detection (extension-based) for choosing object shape

### Phase 2: Image Gallery
**Goal:** Photos displayed as framed images you can browse.

- Image files (png, jpg, webp) rendered as textures on thin plate meshes
- Gallery layout — images on walls of a corridor/room
- Walk up to an image to see it larger
- Pagination/chunking for folders with thousands of images
- LOD — thumbnail at distance, full resolution up close
- Basic interactions: rename, delete, move (drag to another folder door)

### Phase 3: File Interactions
**Goal:** Do real filesystem operations through 3D interactions.

- Select files (click/gaze)
- Context menu: open, rename, copy, move, delete
- Drag files between rooms (move between folders)
- Open text files → content rendered on a surface or opens in terminal
- Open executables → launch in terminal or as subprocess
- Delete → object disappears (or goes to Trash room)
- Create new folder → new door appears
- Create new file → new object appears

### Phase 4: Permission Guards
**Goal:** Linux permissions as physical security.

- Robot NPCs guarding restricted directories
- Permission check on room entry — guard blocks if no access
- sudo prompt when blocked (password dialog or voice challenge)
- Visual indicators for permission levels (color coding, lock icons)
- Guard personalities per directory type (friendly for user dirs, stern for system dirs)
- `chown`/`chmod` as negotiating with or reassigning guards

### Phase 5: Desktop Configuration & Boot
**Goal:** Full boot-to-EDEN experience.

- Default world `.eden` file shipped with EDEN OS
- User config saved to `~/.eden/desktop.eden`
- Editor mode for customizing the environment (terrain, lighting, object placement)
- Auto-play-mode on boot (skip editor, go straight to walking around)
- Hotkey to drop back to editor mode for reconfiguration
- Session management — save/restore open terminals, file positions, etc.

### Phase 6: App Launcher System
**Goal:** Launch real Linux applications from within the 3D world.

- App objects in the world (or a dedicated "apps room")
- Click to launch — opens in embedded window or external X window
- System tray equivalent for running apps
- Integration with .desktop files for installed applications
- Terminal app always available (already built)

### Phase 7: Agent Filesystem Operations
**Goal:** AI companions that navigate and manage files.

- "Go to Pictures and find duplicates" → agent walks there, examines files
- Agent reports findings, asks for confirmation before acting
- Bulk operations: sort by date, organize by type, clean up downloads
- Visual feedback — watch the agent working in the corridor
- Multiple agents for parallel tasks

### Phase 8: Media & Rich Content
**Goal:** Full media support in the 3D environment.

- Video playback on screen objects (render frames to texture)
- Music playback in music room (spatial audio)
- PDF/document viewer on surface objects
- Web browser as a screen object (embedded or texture-rendered)

---

## Technical Notes

### Existing Tech We Can Reuse
- **Terminal on 3D surface**: `EdenTerminal` + `renderToPixels()` + `updateTexture()` — proven pattern
- **Bitmap font rendering**: `EdenTerminalFont.inc` — for filename labels on objects
- **Scene objects**: full transform, texture, mesh system already in place
- **AI companions**: perception, pathfinding, action system, LLM integration
- **Level save/load**: `.eden` format for saving the desktop environment
- **Play mode**: FPS camera, collision, interaction system

### Key Challenges
- **Scale**: thousands of files per folder — need LOD, pagination, streaming
- **Dynamic generation**: rooms generated at runtime from filesystem, not pre-built
- **Real-time sync**: filesystem changes outside EDEN need to update the 3D world
- **Performance**: don't scan entire filesystem at once, lazy-load on room entry
- **Image loading**: async image decode + texture upload for gallery

### File Locations
- Plan: `docs/EDEN_OS_PLAN.md` (this file)
- Terminal: `src/Terminal/EdenTerminal.hpp/.cpp`
- Session files: `session/eden-os.desktop`, `session/eden-session.sh`
- Main app: `examples/terrain_editor/main.cpp`

---

## Monetization Strategy

### Core Principle
Ship early, charge for value, reinvest into development. EDEN OS is a product people use daily (their desktop), which means high retention and recurring revenue potential.

### Phase 1 Revenue: Steam Early Access (Target: Phases 1-3 complete)

**Product:** EDEN OS as a standalone "3D Linux Desktop" experience on Steam.

- Price: $15-25 Early Access, $30-40 at 1.0
- Audience: Linux enthusiasts, ricers/customizers, VR-curious users, people who loved Jurassic Park's "Unix system"
- What ships: folder room generation, image gallery, file interactions, terminal, editor mode
- Steam handles payment, distribution, updates, community
- Wishlists build before launch — start the Steam page as soon as you have a trailer

**Why this works:** Novel desktop environments go viral. A 30-second clip of someone walking through their filesystem in 3D will spread on Reddit/Twitter/HN organically.

### Phase 2 Revenue: World Marketplace

**Product:** User-created desktop worlds sold through an in-app marketplace.

- Creators design custom environments in editor mode (cyberpunk office, forest cabin, space station)
- Sell for $2-10 each, EDEN takes 30% cut
- Free default world ships with EDEN OS
- Seasonal/themed worlds (holiday, sci-fi, minimalist)
- This turns users into creators and creates a content flywheel

### Phase 3 Revenue: AI Agent Subscriptions

**Product:** Premium AI agents that manage your filesystem.

- Free tier: basic file search, simple organization
- Pro tier ($5-10/month): duplicate finder, smart file organizer, photo curator, download cleaner, project archiver
- Agents are visual — you watch them work in your world, which makes the value tangible
- Runs local models for privacy, cloud models for power users

### Phase 4 Revenue: EDEN Engine License

**Product:** The engine itself, for game developers.

- Free for open-source/indie (revenue < $100K)
- Paid license for commercial use ($100-500/year)
- Vulkan-first, Linux-native, built-in AI companion system — differentiators vs Unity/Godot
- This is the long tail play — engine revenue compounds as more games ship on it

### Phase 5 Revenue: Enterprise & Education

**Product:** EDEN OS for teaching Linux and onboarding.

- University site licenses for CS courses — students learn the filesystem by walking through it
- Corporate onboarding for Linux environments
- Custom builds for specific educational curricula
- $500-2000/year per institution

### Revenue Priorities (in order)

1. **Steam Early Access** — fastest path to real money, validates the product
2. **World Marketplace** — low effort once editor is solid, community does the work
3. **AI Agent Subscriptions** — recurring revenue, high margin
4. **Engine License** — long-term compound growth
5. **Enterprise** — high ticket but slow sales cycle, pursue after consumer traction

### Milestones to First Dollar

1. Phases 1-3 functional (folder rooms, gallery, file interactions)
2. 60-second trailer showing someone using their desktop in 3D
3. Steam page live, collecting wishlists
4. Early Access launch
5. Iterate based on player feedback

### Competitive Advantage

Nobody else is building this. There are 3D file managers, VR desktops, and custom Linux shells — but none that combine a full game engine, AI agents, and a filesystem-as-world metaphor into an actual daily-driver desktop. Two months of building has already created something with no direct competitor.

---

*Last updated: 2026-02-26*
