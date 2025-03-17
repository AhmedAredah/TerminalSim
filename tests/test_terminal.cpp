#include <QtTest>
#include "terminal/terminal.h"
#include <QObject>

namespace TerminalSim {

class TestTerminal : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        // Called before the first test
    }

    void testTerminalCreation() {
        // Create a simple terminal
        QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
        interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);

        QVariantMap capacity;
        capacity["max_capacity"] = 100;
        capacity["critical_threshold"] = 0.8;

        Terminal terminal("TestTerminal", interfaces, {}, capacity);

        QCOMPARE(terminal.getTerminalName(), QString("TestTerminal"));
        QCOMPARE(terminal.getMaxCapacity(), 100);
        QCOMPARE(terminal.getContainerCount(), 0);
    }

    void cleanupTestCase() {
        // Called after the last test
    }
};

} // namespace TerminalSim

QTEST_MAIN(TerminalSim::TestTerminal)
#include "test_terminal.moc"
