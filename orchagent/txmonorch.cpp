#include "txmonorch.h"
#include "schema.h"
#include "logger.h"
#include "timer.h"
#include "converter.h"
#include "portsorch.h"

extern PortsOrch* gPortsOrch;

TxMonOrch::TxMonOrch(TableConnector confDbConnector, TableConnector stateDbConnector) :
    Orch(confDbConnector.first, confDbConnector.second),
    m_countersDb(COUNTERS_DB, DBConnector::DEFAULT_UNIXSOCKET, 0),
    m_countersTable(&m_countersDb, COUNTERS_TABLE),
    m_countersPortNameTable(&m_countersDb, COUNTERS_PORT_NAME_MAP),
    m_portsStateTable(stateDbConnector.first, stateDbConnector.second),
    m_pollPeriodSec(DEFAULT_POLLING_PERIOD),
    m_thresholdPackets(DEFAULT_THRESHOLD)
{
    SWSS_LOG_ENTER();
    auto interv = timespec { .tv_sec = m_pollPeriodSec, .tv_nsec = 0 };
    m_timer = new SelectableTimer(interv);
    auto executor = new ExecutableTimer(m_timer, this, "POLLING_TIMER");
    Orch::addExecutor(executor);
    m_timer->start();
}

TxMonOrch::~TxMonOrch()
{
    SWSS_LOG_ENTER();
}

void TxMonOrch::setTimer(uint32_t interval)
{
    SWSS_LOG_ENTER();
    auto interv = timespec { .tv_sec = interval, .tv_nsec = 0 };

    m_timer->setInterval(interv);
    m_timer->reset();
    m_pollPeriodSec = interval;

    SWSS_LOG_DEBUG("m_pollPeriod set [%u]", m_pollPeriodSec);
}

void TxMonOrch::handlePeriodUpdate(const vector<FieldValueTuple>& data)
{
    SWSS_LOG_ENTER();
    uint32_t periodToSet = 0;

    for (auto element : data)
    {
        const auto &field = fvField(element);
        const auto &value = fvValue(element);

        if (field == "value")
        {
            periodToSet = stoi(value);

            if(periodToSet != m_pollPeriodSec)
            {
                setTimer(periodToSet);
            }
        }
        else
        {
            SWSS_LOG_ERROR("Unexpected field %s", field.c_str());
        }
    }
}

void TxMonOrch::handleThresholdUpdate(const vector<FieldValueTuple>& data)
{
    SWSS_LOG_ENTER();
    for (auto element : data)
    {
        const auto &field = fvField(element);
        const auto &value = fvValue(element);

        if (field == "value")
        {
            m_thresholdPackets = stoi(value);
            SWSS_LOG_DEBUG("m_threshold set [%u]", m_thresholdPackets);
        }
        else
        {
            SWSS_LOG_ERROR("Unexpected field %s", field.c_str());
        }
    }
}

void TxMonOrch::createPortsMap()
{
    SWSS_LOG_ENTER();
    map<string, Port> &portsList =  gPortsOrch->getAllPorts();
    for (auto &entry : portsList)
    {
        string name = entry.first;
        Port p = entry.second;
        string oidStr;

        if (p.m_type != Port::PHY)
        {
            continue;
        }

        if (!m_countersPortNameTable.hget("", name, oidStr))
        {
            SWSS_LOG_THROW("Failed to get port oid from counters DB");
        }
        m_portsMap.emplace(p.m_alias, oidStr);
    }
}

void TxMonOrch::setPortsStateDb(string portAlias, PortState state)
{
    SWSS_LOG_ENTER();
    vector<FieldValueTuple> fieldValuesVector;
    fieldValuesVector.emplace_back("port_state", stateNames[state]);
    m_portsStateTable.set(portAlias, fieldValuesVector);
}

void TxMonOrch::getTxErrCounters()
{
    SWSS_LOG_ENTER();
    string tx_err_count;

    for (auto &entry : m_portsMap)
    {
        string portAlias = entry.first;
        string portOid = m_portsMap.find(portAlias)->second;

        if (!m_countersTable.hget(portOid, SAI_PORT_TX_ERR_STAT, tx_err_count))
        {
            setPortsStateDb(portAlias, UNKNOWN);
            SWSS_LOG_THROW("Could not get tx error counters of port %s", portAlias.c_str());
        }

        m_currTxErrCounters[portAlias] = stoul(tx_err_count);
    }
}

void TxMonOrch::updatePortsStateDb()
{
    SWSS_LOG_ENTER();
    for (auto &entry : m_currTxErrCounters)
    {
        PortState portState = OK;
        string portAlias = entry.first;
        uint64_t curr = entry.second;
        uint64_t prev = 0;

        prev = m_prevTxErrCounters[portAlias];

        /* save data for the next update */
        m_prevTxErrCounters[portAlias] = curr;

        if ((curr - prev) >= m_thresholdPackets)
        {
            portState = NOT_OK;
        }
        setPortsStateDb(portAlias, portState);
    }
}

void TxMonOrch::doTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();
    try
    {
        auto it = consumer.m_toSync.begin();
        while (it != consumer.m_toSync.end())
        {
            KeyOpFieldsValuesTuple t = it->second;

            string key = kfvKey(t);
            string op = kfvOp(t);
            vector<FieldValueTuple> fvs = kfvFieldsValues(t);

            if (op == SET_COMMAND)
            {
                if (key == "polling_period")
                {
                    handlePeriodUpdate(fvs);
                }
                else if (key == "threshold")
                {
                    handleThresholdUpdate(fvs);
                }
                else
                {
                    SWSS_LOG_ERROR("Unexpected key %s", key.c_str());
                }
            }
            else
            {
                SWSS_LOG_ERROR("Unexpected operation %s", op.c_str());
            }

            consumer.m_toSync.erase(it++);
        }
    }
    catch (const std::exception& e)
    {
        SWSS_LOG_ERROR("Runtime error: %s", e.what());
    }
}

void TxMonOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();
    try
    {
        if (!gPortsOrch->allPortsReady()) {
            SWSS_LOG_NOTICE("Ports are not ready yet");
            return;
        }

        if (!isPortsMapInitialized)
        {
            createPortsMap();
            isPortsMapInitialized = true;
        }

        getTxErrCounters();
        updatePortsStateDb();
    }
    catch (const std::exception& e)
    {
        SWSS_LOG_ERROR("Runtime error: %s", e.what());
    }
}
