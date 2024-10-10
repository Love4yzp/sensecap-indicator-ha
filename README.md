# Home Assistant & SenseCAP Indicator

<p align="left">
  <a href="https://docs.espressif.com/projects/esp-idf/en/release-v5.1/esp32/">
    <img src="https://img.shields.io/badge/esp--idf-v5.1-00b202" alt="idf-version">
  </a>
</p>

The Home Assistant demo is available in two versions:

- Has only Home Assistant features, which is this repo.
- Which includes the indicator_basis example  -> [branch - feat_home_assistant](https://github.com/Seeed-Solution/SenseCAP_Indicator_ESP32/tree/main/examples/indicator_ha)

There are three demos that show how the indicator integrates with Home Assistant in three ways:

- [x] [MQTT](#mqtt)
- [ ] RESTful API(HTTP)
- [ ] Websocket 

<figure class="third">
    <img align="left" src="./assets/Home Assistant Data.png" width="240"/>
    <img align="center" src="./assets/Home Assistant.png" width="240"/>
    <img align="left" src="./assets/Home Assistant Control(ON).png" width="240"/>
    <img align="center" src="./assets/Home Assistant Control(OFF).png" width="240"/>
</figure>

## MQTT

The wiki for `MQTT` method is provided: [SenseCAP Indicator - Home Assistant Application Development](https://wiki.seeedstudio.com/SenseCAP_Indicator_Application_Home_Assistant/)

### Features

- [x] Wi-Fi Panel - Connect Wi-Fi via screen
- [x] Display config - Control the intensity of the screen
- [x] Home Assistant data - Display Sensor data
- [x] Home Assistant control - the control widgets

### How to use example

Please first read the [User Guide](https://wiki.seeedstudio.com/Sensor/SenseCAP/SenseCAP_Indicator/Get_started_with_SenseCAP_Indicator/) of the SenseCAP Indicator Board to learn about its software and hardware information.

Here are some simple steps to use.

- Step 1: [Install Home Assistant](https://www.home-assistant.io/installation/)
- Step 2: Install MQTT Broker
- Step 3: Add MQTT  integration and config
- Step 4: Modify "configuration.yaml" to add Indicator entity
- Step 5: Edit Dashboard

Add the following to your "configuration.yaml" file
```
# Example configuration.yaml entry
mqtt:
  sensor:
    - unique_id: indicator_temperature
      name: "Indicator Temperature"
      state_topic: "indicator/sensor"
      suggested_display_precision: 1
      unit_of_measurement: "°C"
      value_template: "{{ value_json.temp }}"
    - unique_id: indicator_humidity
      name: "Indicator Humidity"
      state_topic: "indicator/sensor"
      unit_of_measurement: "%"
      value_template: "{{ value_json.humidity }}"
    - unique_id: indicator_co2
      name: "Indicator CO2"
      state_topic: "indicator/sensor"
      unit_of_measurement: "ppm"
      value_template: "{{ value_json.co2 }}"
    - unique_id: indicator_tvoc
      name: "Indicator tVOC"
      state_topic: "indicator/sensor"
      unit_of_measurement: ""
      value_template: "{{ value_json.tvoc }}"
  switch:
    - unique_id: indicator_switch1
      name: "Indicator Switch1"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch1 }}"
      payload_on: '{"switch1":1}'
      payload_off: '{"switch1":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch2
      name: "Indicator Switch2"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch2 }}"
      payload_on: '{"switch2":1}'
      payload_off: '{"switch2":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch3
      name: "Indicator Switch3"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch3 }}"
      payload_on: '{"switch3":1}'
      payload_off: '{"switch3":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch4
      name: "Indicator Switch4"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch4 }}"
      payload_on: '{"switch4":1}'
      payload_off: '{"switch4":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch6
      name: "Indicator Switch6"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch6 }}"
      payload_on: '{"switch6":1}'
      payload_off: '{"switch6":0}'
      state_on: 1
      state_off: 0
    - unique_id: indicator_switch7
      name: "Indicator Switch7"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      value_template: "{{ value_json.switch7 }}"
      payload_on: '{"switch7":1}'
      payload_off: '{"switch7":0}'
      state_on: 1
      state_off: 0
  number:
    - unique_id: indicator_switch5
      name: "Indicator Switch5"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      command_template: '{"switch5": {{ value }} }'
      value_template: "{{ value_json.switch5 }}"
    - unique_id: indicator_switch8
      name: "Indicator Switch8"
      state_topic: "indicator/switch/state"
      command_topic: "indicator/switch/set"
      command_template: '{"switch8": {{ value }} }'
      value_template: "{{ value_json.switch8 }}"
```


Add the following to the raw configuration editor of the dashboard.

```
views:
  - title: Indicator device
    icon: ''
    badges: []
    cards:
      - graph: line
        type: sensor
        detail: 1
        icon: mdi:molecule-co2
        unit: ppm
        entity: sensor.indicator_co2
      - graph: line
        type: sensor
        entity: sensor.indicator_temperature
        detail: 1
        icon: mdi:coolant-temperature
      - graph: line
        type: sensor
        detail: 1
        entity: sensor.indicator_humidity
      - graph: line
        type: sensor
        entity: sensor.indicator_tvoc
        detail: 1
        icon: mdi:air-filter
      - type: entities
        entities:
          - entity: switch.indicator_switch1
          - entity: switch.indicator_switch2
          - entity: switch.indicator_switch3
          - entity: switch.indicator_switch4
          - entity: number.indicator_switch5
          - entity: switch.indicator_switch6
          - entity: switch.indicator_switch7
          - entity: number.indicator_switch8
        title: Indicator control
        show_header_toggle: false
        state_color: true

```

 <img src="./assets/Home Assistant Dashboard.png" />



# Build and Flash

- Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

## Code autocompletion

The easiest way is to install [Clangd](https://github.com/clangd/clangd/releases) and [Clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd) on VSCode.

When you build the project once, it'll have a `build` folder in which `compile_commands.json` will be generated. now you can have fun with Clangd.

# ESP-IDF / CMake / Eclipse / VScode  project for Seeed Studio SenseCAP Indicator development-device exported by SquareLine Studio

## Prerequisites

General note: Please avoid folder-names and filenames containing non-ASCII (special/accented/foreign) characters for the installed tools and your exported projects. Some build-tools and terminals/OS-es don't handle those characters well or interpret them differently, which can cause issues during the build-process.

- Get and install ESP-IDF toolchain and its dependencies.
  [ESP-IDF Get started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
  - Install ESP-IDF on Windows: [Description page](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/windows-setup.html)
  - There's a ESP-related IDE too, made by Espressif, containing ESP-IDF, called Espressif-IDE which is based on Eclipse CDT. [Espressif-IDE](https://github.com/espressif/idf-eclipse-plugin/blob/master/docs/Espressif-IDE.md)
    - Get the [ESP-IDF offline Windows installer](https://dl.espressif.com/dl/idf-installer/esp-idf-tools-setup-offline-5.1.2.exe?) or [Espressif-IDE Windows installer](https://dl.espressif.com/dl/idf-installer/espressif-ide-setup-2.11.1-with-esp-idf-5.1.2.exe?)
    - Install it on your Windows system accepting all offered options and default settings. This automagically installs Python, git, CMake, etc all at once under C:\Espressif folder.
    - You can start building in command-line from the PowerShell/CMD entries created in the start-menu, but with the help of the included build.bat you can build on a normal commandline too
    - Or you can build the project in the IDE GUI, see 'Usage' section.
  
  - Install ESP-IDF on Linux/MacOS: [Description page](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/linux-macos-setup.html)
    - Get Python3, git, cmake, ninja-build, wget, flex, bison, libusb, etc
    - In a directory (preferably a created $HOME/esp) type `git clone --recursive https://github.com/espressif/esp-idf.git`
      - (This gets the latest version into an 'esp-idf' subfolder. In ESP-IDF v5.0 a patch is needed to set PSRAM frequency to 120MHz.)
    - In the esp-idf directory run `./install.sh`, then you can add ESP32S3 support (needed for SenseCAP indicator) by `./install.sh esp32s3`
    - To use ESP-IDF ('idf.py') you need to type `. ./export.sh`, but it's only active in the current shell.
      - (To call it in any shell later easily, add `alias get_idf='. $HOME/esp/esp-idf/export.sh'` to your .bashrc file, so a `get_idf` command will get the IDF environment temporarily.)


## Usage

### Building the project and flashing to the device from command-line
- On windows you should run the special command line which ESP-IDF created so you can use the upcoming commands. (There you can navigate into the project-folder.)
- In this folder type `idf.py build` command to build the binaries for the SenseCAP indicator (in 'build' folder, a full `sdkconfig` file will be created if it didn't exist)
  (If the compiled application .bin file doesn't fit into the 'app' partition a size-check error will follow. In this project the app partition is set to 7MB in 'partitions.csv' file, total flash of SenseCAP is 8MB)
- To flash it to the device use `idf.py flash` command, after flashing or when switched on, it will start automatically. (If you issue this command first, it will build the project beforehand, if not already built.)
- (To monitor the output of the running application you can use `idf.py monitor` command. It will restart the device. It can be used together with flashing: `idf.py flash monitor`. Press Control+] to exit the monitor-shell.)
- (The SenseCAP device port is detected automatically, but you can specify it with -p <portname> argument, e.g. `idf.py -p /dev/ttyUSB0 monitor` or `idf.py -p COM6 flash`.)

### Project customization
- ESP-IDF uses 'menuconfig' to create the 'sdkconfig' (based on Kconfig) file which holds the whole configuration of the project (including detailed component settings), use the `idf.py menuconfig` command to start it
  - (sdkconfig in an ESP-IDF project has LVGL-configuration in the 'Component config' subcategory and 'lv_conf.h' as such is no longer used to configure LVGL features.)
- In this project all built-in ('montserrat') LVGL fonts are enabled. If you want to add/remove the LVGL built-infonts to select only the fonts your project needs look in submenu: Component config / LVGL configuration / Font usage / Enable built-in fonts.
- Above ESP-IDF 5.0 the 120MHz speed option for the PSRAM (necessary for SenseCAP Indicator) is selectable (no patch needed) after enabling 'Make experimental features visible', and is enabled in submenu: Components / ESP PSRAM / SPI RAM config / Set RAM clock speed
- Exit from menuconfig by Q and answer Y to save the modifications to file 'sdkconfig'.
- (If you want a clean sdkconfig file containing only the modifications you can type `idf.py save-defconfig` to get it. The resulting `sdkconfig.defaults` file will be used if `sdkconfig` file is not found.)
- (You can edit 'sdkconfig.defaults' directly by hand, and if you delete 'sdkconfig' and rebuild the project, it will be recreated. But please leave 'CONFIG_LV_MEM_CUSTOM=y' setting untouched, otherwise you might face a freeze.)


## Alternative build-methods/tools

- Command-line CMake-based building works as well, but the above-mentioned 'export.sh' or 'get_idf' should be used beforehand to pull the IDF environment into the terminal,
  - You need to create a 'build' folder (to avoid littering the project-folder), cd into it then type `cmake -G "Ninja" ..` to generate the files (or `cmake ..` for the slower 'Make' based build)
  - When the makefile gets generated you can build it with `ninja -j 4` command ('-j 4' tells it to use 4 CPUs if possible) and flash with `ninja flash` command
  - To make this easier there's a `build.sh` and `build.bat` file which does this plus cleans build-folder before every build, and then `flash.sh` or `flash.bat` uploads the program into the device.
    - (You might need to edit the build.bat and flash.bat files and rewrite the version number to your installed ESP-IDF version for them to work.)

- For GUI-based development Visual Studio Code and Eclipse-IDE basic project files are included too so you can build in these tools too, as the project files are based on CMake.

  - Eclipse CDT has a plugin described at [Eclipse Marketplace ESP-IDF plugin](https://marketplace.eclipse.org/content/esp-idf-eclipse-plugin) and [ESP-IDF plugin github-page](https://github.com/espressif/idf-eclipse-plugin)
    - In 'Help' / 'Install new software' submenu you can install 'Espressif IDF' from 'https://dl.espressif.com/dl/idf-eclipse-plugin/updates/latest/' by following the instructions
  - (But if you installed Espressif-IDE in the 'Prerequisites' section this will probably be most compatible with ESP-IDF development.)
    - Follow the instructions at the GitHub-page (browsing existing ESP-IDF folder when asked) and you'll get an Eclipse project environment to build and flash.
    - To open your exported project-template, use menu File / Import, select 'Existing IDF Project' and select the project folder. Pressing Build (hammer) button will compile the project to .elf and .bin files in 'build' folder.

  - VScode has an Espressif-IDF extension that should be used, installable from within VScode.
    - [Installation](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md)
    - [Basic Use](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/basic_use.md)
  