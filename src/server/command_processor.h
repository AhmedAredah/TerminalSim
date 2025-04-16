#pragma once

#include <QObject>
#include <QVariant>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <functional>

#include "terminal/terminal_graph.h"

namespace TerminalSim {

/**
 * @brief Processes commands for the terminal graph server
 */
class CommandProcessor : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Command handler function type
     */
    using CommandHandler = std::function<QVariant(const QVariantMap&)>;
    
    /**
     * @brief Construct a command processor
     * @param graph Terminal graph to work with
     * @param parent Parent object
     */
    explicit CommandProcessor(TerminalGraph* graph, QObject* parent = nullptr);
    ~CommandProcessor();
    
    /**
     * @brief Process a command
     * @param command Command name
     * @param params Command parameters
     * @return Command result
     */
    QVariant processCommand(const QString& command, const QVariantMap& params);
    
    /**
     * @brief Process a command in JSON format
     * @param commandObject JSON command object
     * @return JSON response object
     */
    QJsonObject processJsonCommand(const QJsonObject& commandObject);
    
private:
    /**
     * @brief Register all command handlers
     */
    void registerCommands();
    
    /**
     * @brief Register a command handler
     * @param command Command name
     * @param handler Command handler function
     */
    void registerCommand(const QString& command, CommandHandler handler);
    
    /**
     * @brief Get terminal from ID parameter
     * @param params Command parameters containing terminal_id
     * @return Terminal pointer or nullptr if not found
     */
    Terminal* getTerminalFromParams(const QVariantMap& params);

    /**
     * @brief Maps a command name to its corresponding event name for client
     * response.
     * This function translates command names (like "add_terminal") to
     * event names (like "terminalAdded") that the client expects in the
     * response. The client uses these event names to match responses with
     * waiting commands.
     * @param command The command name to translate
     * @return The corresponding event name that the client is expecting
     * @note Even for failed commands, the appropriate event name should
     * be returned so that the client can properly handle errors and unblock
     * waiting calls.
     */
    QString determineEventName(const QString &command);

    // Command handlers
    QVariant handlePing(const QVariantMap& params);
    QVariant handleSerializeGraph(const QVariantMap& params);
    QVariant handleDeserializeGraph(const QVariantMap& params);
    QVariant handleResetServer(const QVariantMap& params);
    QVariant handleAddTerminal(const QVariantMap& params);
    QVariant handleAddTerminals(const QVariantMap &params);
    QVariant handleAddRoute(const QVariantMap& params);
    QVariant handleAddRoutes(const QVariantMap &params);
    QVariant handleFindShortestPath(const QVariantMap& params);
    QVariant handleFindTopPaths(const QVariantMap& params);
    QVariant handleGetTerminal(const QVariantMap& params);
    QVariant handleAddContainer(const QVariantMap& params);
    
    // Data serialization helpers
    QVariant serializeResponse(const QVariant& result);
    QVariantMap deserializeParams(const QVariantMap& params);
    
private:
    // Terminal graph
    TerminalGraph* m_graph;
    
    // Command registry
    QMap<QString, CommandHandler> m_commandHandlers;
    
    // Thread safety
    mutable QMutex m_mutex;
};

} // namespace TerminalSim
