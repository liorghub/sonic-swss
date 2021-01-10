#ifndef __TXMONORCH__
#define __TXMONORCH__

#include "table.h"
#include "orch.h"
#include "selectabletimer.h"

using namespace swss;
using namespace std;

#define DEFAULT_POLLING_PERIOD 30
#define DEFAULT_THRESHOLD 10
#define STATES_NUMBER 3
#define SAI_PORT_TX_ERR_STAT "SAI_PORT_STAT_IF_OUT_ERRORS"

enum PortState {OK, NOT_OK, UNKNOWN};
static const array<string, STATES_NUMBER> stateNames = {"OK", "NOT_OK", "UNKNOWN"};

class TxMonOrch: public Orch
{
public:
    TxMonOrch(TableConnector confDbConnector, TableConnector stateDbConnector);
    ~TxMonOrch(void);
    void doTask(Consumer &consumer);
    void doTask(SelectableTimer &timer);

private:
    DBConnector m_countersDb;
    Table m_countersTable;
    Table m_countersPortNameTable;

    /* port alias to port state */
    Table m_portsStateTable;

    SelectableTimer *m_timer = nullptr;
    /* time in seconds to wait between counter samples */
    uint32_t m_pollPeriodSec;
    uint32_t m_thresholdPackets;

    bool isPortsMapInitialized = false;
    /* port alias to port oid */
    map<string, string> m_portsMap;

    /* port alias to port tx-error stats */
    map<string, uint64_t> m_currTxErrCounters;
    map<string, uint64_t> m_prevTxErrCounters;

    void handlePeriodUpdate(const vector<FieldValueTuple>& data);
    void handleThresholdUpdate(const vector<FieldValueTuple>& data);
    void setTimer(uint32_t interval);
    void createPortsMap();
    void getTxErrCounters();
    void setPortsStateDb(string portAlias, PortState state);
    void updatePortsStateDb();
};

#endif

