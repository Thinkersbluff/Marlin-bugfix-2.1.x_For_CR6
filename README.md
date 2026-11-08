**NOTES TO CR6 USERS WHO WISH TO PARTICIPATE IN THIS PROJECT:**

- Thinkyhead is controlling the actual CR6-SE integration project and is making pre-compiled firmwares for stock-configuration CR6-SE printers available for testing at [https://github.com/MarlinFirmware/Marlin/pull/19958](https://github.com/MarlinFirmware/Marlin/pull/19958#issuecomment-6404508230).

- The source code from which those files are built is available at this branch of Thinkyhead/Marlin: https://github.com/thinkyhead/Marlin/tree/bf2_creality_CR-6_SE_PR
- The configuration file examples from which those bin files are being compiled are available here:
 •	V4.5.2: https://github.com/thinkyhead/Configurations/tree/651df1a524c4/config/examples/Creality/CR-6%20SE/CrealityV452
 •	V4.5.3: the same files with MOTHERBOARD BOARD_CREALITY_V453.
 •	SKR CR6 with the stock screen: https://github.com/thinkyhead/Configurations/tree/651df1a524c4/config/examples/Creality/CR-6%20SE/BigTreeTech%20SKR%20CR6 with ENDSTOP_NOISE_THRESHOLD 2 enabled.


**CAUTION:**
- The Marlin/Configuration.h, Marlin/Configuration_adv.h and platformio.ini files in the root of this fork are specifically tailored by Thinkersbluff to run on Thinkersbluff's own customized CR6-SE.  They may help to inspire and inform your own efforts to tailor those files to your own non-stock CR6 machines, but they are not likely to work with your machines as-is.
- Issues and Discussions on this repository are Thinkersbluff's way of keeping his own head clear as to what has been discovered during the testing that may require feedback to the upstream team or may lead to future customizations of the CR6 DWIN_SET.  Nothing on this repo represents any opinion or commitment by Thinkyhead or the upstream Marlin team.
If you would like to contribute to the Issues and Discussions or to add your own, please feel free to do that here.

Please understand:
 - We are all volunteers, here.
 - We are motivated by what we might achieve for ourselves and for the CR6 Community.
 - We are under no obligation to actually finish what we have started here.
 - If you decide to flash this code to your own printer, you do so entirely at your own risk.
 - There is no warranty expressed or implied that this code will work as described or as intended, on your particular printer.

---

<p align="center">
    <a href="/LICENSE"><img alt="GPL-V3.0 License" src="https://img.shields.io/github/license/marlinfirmware/marlin.svg"></a>
    <a href="//github.com/MarlinFirmware/Marlin/graphs/contributors"><img alt="Contributors" src="https://img.shields.io/github/contributors/marlinfirmware/marlin.svg"></a>
    <a href="//github.com/MarlinFirmware/Marlin/releases"><img alt="Last Release Date" src="https://img.shields.io/github/release-date/MarlinFirmware/Marlin"></a>
    <a href="//github.com/MarlinFirmware/Marlin/actions/workflows/ci-build-tests.yml"><img alt="CI Status" src="https://github.com/MarlinFirmware/Marlin/actions/workflows/ci-build-tests.yml/badge.svg"></a>
    <a href="//github.com/sponsors/thinkyhead"><img alt="GitHub Sponsors" src="https://img.shields.io/github/sponsors/thinkyhead?color=db61a2"></a>
    <br />
    <a href="//bsky.app/profile/marlinfw.org"><img alt="Follow marlinfw.org on Bluesky" src="https://img.shields.io/badge/Follow%20@marlinfw.org-0085ff?logo=bluesky&logoColor=white"></a>
    <a href="//fosstodon.org/@marlinfirmware"><img alt="Follow MarlinFirmware on Mastodon" src="https://img.shields.io/mastodon/follow/109450200866020466?domain=https%3A%2F%2Ffosstodon.org&logoColor=%2300B&style=social"></a>
</p>

Official documentation can be found at the [Marlin Home Page](//marlinfw.org/).

Please test this firmware and let us know if it misbehaves in any way. Volunteers are standing by!

---

## This firmware is from the Marlin 2.1 Bugfix Branch

**Not for production use. Use with caution!**
This branch is for patches to the latest 2.1.x release version specific to PR#19588. 
Once we have this patched version stabilized to work with the CR6 DWIN_SET firmware, the upstream branch on Thinkyhead/Marlin will form the basis for how CR6 printers are merged back into the formal Marlin 2.1.x release.

## Example Configurations

Before you can build Marlin for your machine you'll need a configuration for your specific hardware. 


## Building Marlin 2.1

To build and upload Marlin you will use one of these tools:

- The free [Visual Studio Code](//code.visualstudio.com/download) using the [Auto Build Marlin](//marlinfw.org/docs/basics/auto_build_marlin.html) extension.
- Marlin is optimized to build with the [PlatformIO IDE](//platformio.org/) extension for Visual Studio Code.
- You can also use VSCode with devcontainer : See [Installing Marlin (VSCode devcontainer)](https://marlinfw.org/docs/basics/install_devcontainer_vscode.html).
- You can still build Marlin with [Arduino IDE](//www.arduino.cc/en/main/software) : See [Building Marlin with Arduino](//marlinfw.org/docs/basics/install_arduino.html). We hope to improve the Arduino build experience, but at this time, PlatformIO is the preferred choice.


### Supported Platforms

| Platform | MCU | Example Boards |
| --- | --- | --- |
| [STM32F103](//www.st.com/en/microcontrollers-microprocessors/stm32f103.html) | ARM® Cortex-M3 | Creality 4.5.2, 4.5.3, 1.1.0.3 , BTT SKR CR6 v1.0 |


## Discord

The [CR6 Community Discord](https://discord.gg/c7cVhFqXT) is the best place to discuss issues with using the CF6.1 Marlin firmware and this 2.1.x Marlin, on CR6 printers, with other CR6 Community members.

The [Marlin Firmware Discord](//discord.gg/marlin-firmware-461605380783472640) is a great place to discuss issues with Marlin users and developers, get interactive help with troubleshooting, and build on your best ideas to improve to Marlin in tandem with the most active members of the development team.

<p align="center">
  <a target="_blank" href="https://discord.gg/marlin-firmware-461605380783472640">
    <img src="https://dcbadge.limes.pink/api/server/https://discord.gg/marlin-firmware-461605380783472640">
  </a>
</p>

## Other Marlin Support

The Issue Queue is reserved for Bug Reports and Feature Requests. Please use the following resources for help with configuration and troubleshooting:

- [Marlin Documentation](//marlinfw.org) - Official Marlin documentation
- ["Marlin Firmware"](//www.facebook.com/groups/1049718498464482/) Facebook Group
- [Marlin Forum](//forums.reprap.org/list.php?415) at RepRap.org
- ["Marlin Firmware for 3D Printers"](//www.facebook.com/groups/3Dtechtalk/) Facebook Group
- [Marlin Configuration](//www.youtube.com/results?search_query=marlin+configuration) playlist on YouTube

## Contributors

Marlin is constantly improving thanks to a huge number of contributors from all over the world bringing their specialties and talents. Huge thanks are due to [all the contributors](//github.com/MarlinFirmware/Marlin/graphs/contributors) who regularly patch up bugs, help direct traffic, and basically keep Marlin from falling apart. Marlin's continued existence would not be possible without them.

Marlin Firmware original logo design by Ahmet Cem TURAN [@ahmetcemturan](//github.com/ahmetcemturan).

## Project Leadership

| Name | Role | Link | Donate |
| --- | --- | --- | --- |
| 🇺🇸 Scott Lahteine | Project Lead | [[@thinkyhead](//github.com/thinkyhead)] | [❤️ Donate](//marlinfw.org/docs/development/contributing.html#donate) |
| 🇳🇿 Peter Ellens | Admin | [[@ellensp](//github.com/ellensp)] | [❤️ Donate](//ko-fi.com/ellensp) |
| 🇬🇧 Chris Pepper | Admin | [[@p3p](//github.com/p3p)] |
| 🇺🇸 Keith Bennett | Admin | [[@thisiskeithb](//github.com/thisiskeithb)] | [❤️ Donate](//github.com/sponsors/thisiskeithb) |
| 🇺🇸 Roxanne Neufeld | Admin | [[@Roxy-3D](//github.com/Roxy-3D)] |
| 🇺🇸 Jason Smith | Admin | [[@sjasonsmith](//github.com/sjasonsmith)] |
| 🇧🇷 Victor Oliveira | Admin | [[@rhapsodyv](//github.com/rhapsodyv)] |
| 🇺🇸 Bob Kuhn | Admin | [[@Bob-the-Kuhn](//github.com/Bob-the-Kuhn)] |
| 🇳🇱 Erik van der Zalm | Founder | [[@ErikZalm](//github.com/ErikZalm)] |

## Star History

<a id="starchart" href="//star-history.com/#MarlinFirmware/Marlin&Date">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=MarlinFirmware/Marlin&type=Date&theme=dark" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=MarlinFirmware/Marlin&type=Date" />
    <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=MarlinFirmware/Marlin&type=Date" />
  </picture>
</a>

## License

Marlin is published under the [GPL license](/LICENSE) because we believe in open development. The GPL comes with both rights and obligations. Whether you use Marlin firmware as the driver for your open or closed-source product, you must keep Marlin open, and you must provide your compatible Marlin source code to end users upon request. The most straightforward way to comply with the Marlin license is to make a fork of Marlin on GitHub, perform your modifications, and direct users to your modified fork.
