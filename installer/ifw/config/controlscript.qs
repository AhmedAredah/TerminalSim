// Qt Installer Framework control script. Runs in the installer's QScript
// engine; documented at https://doc.qt.io/qtinstallerframework/scripting.html

function Controller() {
    // Hide marketing pages we do not need.
    installer.setDefaultPageVisible(QInstaller.Introduction,    true);
    installer.setDefaultPageVisible(QInstaller.TargetDirectory, true);
    installer.setDefaultPageVisible(QInstaller.ComponentSelection, true);
    installer.setDefaultPageVisible(QInstaller.LicenseCheck,    true);
    installer.setDefaultPageVisible(QInstaller.StartMenuSelection, systemInfo.productType === "windows");
    installer.setDefaultPageVisible(QInstaller.ReadyForInstallation, true);
    installer.setDefaultPageVisible(QInstaller.PerformInstallation, true);
    installer.setDefaultPageVisible(QInstaller.FinishedPage,    true);
}

Controller.prototype.IntroductionPageCallback = function () {
    var page = gui.pageWidgetByObjectName("IntroductionPage");
    if (page) {
        page.title = "Welcome to TerminalSim";
        page.MessageLabel.setText(
            "<p>This installer will set up the TerminalSim simulation server " +
            "on your machine.</p>" +
            "<p>TerminalSim runs as a background service and communicates " +
            "with CargoNetSim and other clients over RabbitMQ.</p>");
    }
};
