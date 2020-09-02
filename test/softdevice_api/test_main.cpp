// Logging support

#include <iostream>
#include <mutex>
#include <test_setup.h>

std::ostream &nrfLogStream(std::cout);
std::mutex nrfLogMutex;

#define CATCH_CONFIG_RUNNER
#include "catch2/catch.hpp"

int main(int argc, char *argv[])
{
    Catch::Session session;

    auto serialPortA = std::string{};
    auto serialPortB = std::string{};
    auto hardwareInfo = std::string{};
    auto baudRate    = defaultBaudRate;

    using namespace Catch::clara;

    const auto cli =
        session.cli() |
        Opt(serialPortA, "serial-port")["--port-a"]("serial port A, usually BLE central") |
        Opt(serialPortB, "serial-port")["--port-b"]("serial port B, usually BLE peripheral") |
        Opt(test::ConfiguredEnvironment.baudRate, "baud-rate")["--baud-rate"]("baud rate") |
        Opt(test::ConfiguredEnvironment.responseTimeout, "milliseconds")["--response-timeout"]("Transport response timeout") |
        Opt(test::ConfiguredEnvironment.retransmissionInterval, "milliseconds")["--retransmission-interval"]("Transport retransmission interval") |
        Opt(test::ConfiguredEnvironment.mtu, "size")["--ble-mtu"]("Default BLE MTU, may be ignored in some tests") |
        Opt(test::ConfiguredEnvironment.numberOfIterations,
            "count")["--iterations"]("number of iterations (for tests supporting that)") |
        Opt(
            [&](const std::string &value) {
                if (!value.empty())
                {
                    try
                    {
                        test::ConfiguredEnvironment.driverLogLevel =
                            testutil::parseLogSeverity(value);
                        test::ConfiguredEnvironment.driverLogLevelSet = true;
                    }
                    catch (std::invalid_argument &)
                    {
                        INFO("Log level '" << value << "' not supported.");
                    }
                }
            },
            "trace|debug|info|warning|error|fatal")["--log-level"]("pc-ble-driver log level") |
        Opt(hardwareInfo,
            "text")["--hardware-info"]("hardware info text to show in test reports");

    session.cli(cli);

    const auto exitCode = session.applyCommandLine(argc, argv);

    if (exitCode != 0)
        return exitCode;

    if (!serialPortA.empty())
    {
        test::ConfiguredEnvironment.serialPorts.emplace_back(serialPortA, baudRate);
    }

    if (!serialPortB.empty())
    {
        test::ConfiguredEnvironment.serialPorts.emplace_back(serialPortB, baudRate);
    }

    if (!hardwareInfo.empty())
    {
        test::ConfiguredEnvironment.hardwareInfo = hardwareInfo;
    }
    else
    {
        test::ConfiguredEnvironment.hardwareInfo = "No hardware info provided.";
    }

    return session.run();
}
