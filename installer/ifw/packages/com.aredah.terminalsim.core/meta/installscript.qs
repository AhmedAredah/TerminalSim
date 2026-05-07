// Per-component install script for the TerminalSim core package.

function Component() {}

Component.prototype.createOperations = function () {
    component.createOperations();

    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut",
            "@TargetDir@/bin/terminal_simulation.exe",
            "@StartMenuDir@/TerminalSim Server.lnk",
            "workingDirectory=@TargetDir@/bin",
            "iconPath=@TargetDir@/bin/terminal_simulation.exe");

        component.addOperation("CreateShortcut",
            "@TargetDir@/maintenancetool.exe",
            "@StartMenuDir@/Uninstall TerminalSim.lnk");
    }
};
