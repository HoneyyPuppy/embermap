#include "RoutingTable.h"

RoutingTable::RoutingTable(uint8_t satelliteId)
    : m_satelliteId(satelliteId), m_parentCount(0), m_myCost(999.0), m_hasRoute(false), m_parentRoutePathLen(0), m_allowedCount(0) {
    memset(m_nextHopMac, 0, 6);
}

void RoutingTable::purgeExpired(unsigned long timeout) {
    unsigned long now = millis();
    bool changed = false;
    for (int i = 0; i < m_parentCount; i++) {
        if (now - m_candidates[i].lastSeen >= timeout) {
            for (int j = i; j < m_parentCount - 1; j++) {
                m_candidates[j] = m_candidates[j+1];
            }
            m_parentCount--;
            i--;
            changed = true;
        }
    }
    if (changed) {
        syncPrimary();
    }
}

void RoutingTable::updateCandidate(const uint8_t *mac, float cost, const uint8_t *routePath, uint8_t routePathLen) {
    unsigned long now = millis();
    int foundIdx = -1;

    for (int i = 0; i < m_parentCount; i++) {
        if (memcmp(m_candidates[i].mac, mac, 6) == 0) {
            foundIdx = i;
            break;
        }
    }

    if (foundIdx != -1) {
        m_candidates[foundIdx].cost = cost;
        m_candidates[foundIdx].lastSeen = now;
        memcpy(m_candidates[foundIdx].routePath, routePath, routePathLen);
        m_candidates[foundIdx].routePathLen = routePathLen;
    } else {
        if (m_parentCount < MAX_PARENT_CANDIDATES) {
            memcpy(m_candidates[m_parentCount].mac, mac, 6);
            m_candidates[m_parentCount].cost = cost;
            m_candidates[m_parentCount].lastSeen = now;
            memcpy(m_candidates[m_parentCount].routePath, routePath, routePathLen);
            m_candidates[m_parentCount].routePathLen = routePathLen;
            m_parentCount++;
        } else {
            if (cost < m_candidates[MAX_PARENT_CANDIDATES - 1].cost) {
                memcpy(m_candidates[MAX_PARENT_CANDIDATES - 1].mac, mac, 6);
                m_candidates[MAX_PARENT_CANDIDATES - 1].cost = cost;
                m_candidates[MAX_PARENT_CANDIDATES - 1].lastSeen = now;
                memcpy(m_candidates[MAX_PARENT_CANDIDATES - 1].routePath, routePath, routePathLen);
                m_candidates[MAX_PARENT_CANDIDATES - 1].routePathLen = routePathLen;
            } else {
                return;
            }
        }
    }

    sortCandidates();
    syncPrimary();
}

void RoutingTable::removeCandidateAtIndex(uint8_t idx) {
    if (idx < m_parentCount) {
        for (int j = idx; j < m_parentCount - 1; j++) {
            m_candidates[j] = m_candidates[j+1];
        }
        m_parentCount--;
        syncPrimary();
    }
}

void RoutingTable::sortCandidates() {
    for (int i = 0; i < m_parentCount - 1; i++) {
        for (int j = 0; j < m_parentCount - i - 1; j++) {
            if (m_candidates[j].cost > m_candidates[j+1].cost) {
                ParentCandidate temp = m_candidates[j];
                m_candidates[j] = m_candidates[j+1];
                m_candidates[j+1] = temp;
            }
        }
    }
}

void RoutingTable::syncPrimary() {
    if (m_parentCount > 0) {
        memcpy(m_nextHopMac, m_candidates[0].mac, 6);
        m_myCost = m_candidates[0].cost;
        m_hasRoute = true;
        memcpy(m_parentRoutePath, m_candidates[0].routePath, m_candidates[0].routePathLen);
        m_parentRoutePathLen = m_candidates[0].routePathLen;
    } else {
        m_hasRoute = false;
        m_myCost = 999.0;
        memset(m_nextHopMac, 0, 6);
        m_parentRoutePathLen = 0;
    }
}

void RoutingTable::setAllowedNeighbors(const uint8_t* allowedList, uint8_t count) {
    m_allowedCount = count > 10 ? 10 : count;
    if (m_allowedCount > 0 && allowedList != nullptr) {
        memcpy(m_allowedNeighbors, allowedList, m_allowedCount);
    }
}

bool RoutingTable::isAllowedNeighbor(uint8_t neighborId) const {
    if (m_allowedCount == 0 || (m_allowedCount == 1 && m_allowedNeighbors[0] == 0xFF)) {
        return true;
    }
    for (uint8_t i = 0; i < m_allowedCount; i++) {
        if (m_allowedNeighbors[i] == neighborId) {
            return true;
        }
    }
    return false;
}
