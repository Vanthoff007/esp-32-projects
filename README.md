# ESP32 Projects

A collection of independent ESP32 projects. Each project lives in its own folder under `projects/` with its own firmware, documentation and tools, and builds on its own. Nothing at the repository root is needed to build a project.

## Projects

| Project | Board | Status | Summary |
|---|---|---|---|
| [Night Sentinel](projects/night-sentinel/) | DFRobot DFR1154 (ESP32-S3 AI Camera) | Working end to end; modular firmware 0.10.0 (platform / models / app). Field tests and model experiments in progress | Night-time person alert that runs entirely on the camera board: IR night vision, on-device person detection, then a spoken warning, phone push and JPEG evidence on SD |

## Repository layout

```text
esp32-projects/
├── README.md            this file: overview and project index
├── LICENSE              Apache-2.0, covers every project unless a project says otherwise
├── .gitignore           rules shared by all projects (build output, sdkconfig, editors, Python)
└── projects/
    └── night-sentinel/  one complete ESP-IDF project (see its README)
```

Shared `docs/` or `tools/` folders at the root will be added once two or more projects actually share something. Until then, everything a project needs stays inside its folder.

## Requirements

What every project here assumes. Each project's README lists its own board, extra hardware and components.

- **Framework:** [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) (Night Sentinel uses v6.1). Components come from the [ESP Component Registry](https://components.espressif.com/) and are pinned in each project's `dependencies.lock`.
- **Machine:** developed on Windows 11 with the ESP-IDF Installation Manager (EIM). Commands in the docs are PowerShell.
- **Python tools (optional):** some projects ship PC-side scripts in their `tools/` folder, managed with [uv](https://docs.astral.sh/uv/).

## Building and running a project

Every project is a normal ESP-IDF project, so build it from **its own folder**, not from the repository root:

```powershell
git clone https://github.com/Vanthoff007/esp-32-projects.git D:\Projects\esp32-projects
cd D:\Projects\esp32-projects\projects\night-sentinel
idf.py set-target esp32s3        # first time only
idf.py build
idf.py -p COM5 flash monitor     # use your board's port
```

Run these in a terminal with ESP-IDF loaded. In VS Code, open the **project folder** (for example `projects/night-sentinel`), not the repository root: the ESP-IDF extension looks for `CMakeLists.txt` in the folder you open.

The project README covers its full setup, configuration and first run.

## Adding a new project

1. Create `projects/<project-name>/`, using a short kebab-case name (for example `projects/weather-station/`).
2. Put a complete, self-contained project inside: its top-level `CMakeLists.txt`, `main/`, `components/`, `sdkconfig.defaults` and `partitions.csv` if it needs one. `idf.py create-project <name>` inside `projects/` gives you a starting point.
3. Add a `README.md` covering what it does, the hardware, dependencies, how to build and flash, and its status.
4. Add a project `.gitignore` only for rules specific to that project. The root `.gitignore` already covers `build/`, `sdkconfig`, `managed_components/` and editor files.
5. Commit `sdkconfig.defaults` and `dependencies.lock`, never `sdkconfig`: it can contain Wi-Fi passwords.
6. Add a row to the **Projects** table above.
7. Start commit messages with the project name, for example `night-sentinel: add SD logging`, so each project's history is easy to filter (`git log -- projects/<name>`).

## License

[Apache License 2.0](LICENSE).
