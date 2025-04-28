// Edge.h
#pragma once

#include "common.h"
#include <QString>
#include <utility>

namespace GraphLib
{

template <typename VertexIdType, typename WeightType> class Edge
{
public:
    // Add a default constructor
    Edge() = default;

    Edge(VertexIdType source, VertexIdType target, WeightType weight,
         TerminalSim::TransportationMode mode =
             TerminalSim::TransportationMode::Any)
        : m_source(std::move(source))
        , m_target(std::move(target))
        , m_weight(std::move(weight))
        , m_mode(mode)
    {
    }

    VertexIdType source() const
    {
        return m_source;
    }
    VertexIdType target() const
    {
        return m_target;
    }
    WeightType weight() const
    {
        return m_weight;
    }
    TerminalSim::TransportationMode mode() const
    {
        return m_mode;
    }

    void setWeight(WeightType weight)
    {
        m_weight = std::move(weight);
    }
    void setMode(TerminalSim::TransportationMode mode)
    {
        m_mode = mode;
    }

    QString toString() const
    {
        QString result =
            QString("%1 -> %2 (Weight: %3, Mode: %4)")
                .arg(m_source)
                .arg(m_target)
                .arg(m_weight)
                .arg(
                    TerminalSim::EnumUtils::transportationModeToString(m_mode));
        return result;
    }

    bool hasSameOriginDestination(const Edge &other) const
    {
        return m_source == other.m_source && m_target == other.m_target;
    }

    bool operator==(const Edge &other) const
    {
        return m_source == other.m_source && m_target == other.m_target
               && m_weight == other.m_weight && m_mode == other.m_mode;
    }

private:
    VertexIdType                    m_source;
    VertexIdType                    m_target;
    WeightType                      m_weight;
    TerminalSim::TransportationMode m_mode;
};

} // namespace GraphLib
