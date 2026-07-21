#include "RoutingTable.h"

RoutingTable::RoutingTable(uint8_t satelliteId)
    : m_satelliteId(satelliteId), m_neighborCount(0), m_hasRoute(false), m_myHopCount(255), m_basePotential(9999.0), m_parentRoutePathLen(0), m_allowedCount(0) {
    memset(m_nextHopMac, 0, 6);
}

void RoutingTable::purgeExpired(unsigned long timeout) {
    unsigned long now = millis();
    for (int i = 0; i < m_neighborCount; i++) {
        if (now - m_neighbors[i].lastSeen >= timeout) {
            for (int j = i; j < m_neighborCount - 1; j++) {
                m_neighbors[j] = m_neighbors[j+1];
            }
            m_neighborCount--;
            i--;
        }
    }
}

void RoutingTable::updateNeighbor(const uint8_t *mac, float potential, uint8_t hopCount, const uint8_t *routePath, uint8_t routePathLen, int rssi) {
    unsigned long now = millis();
    int foundIdx = -1;
    float linkCost = calculateLinkCost(rssi);

    for (int i = 0; i < m_neighborCount; i++) {
        if (memcmp(m_neighbors[i].mac, mac, 6) == 0) {
            foundIdx = i;
            break;
        }
    }

    if (foundIdx != -1) {
        m_neighbors[foundIdx].potential = potential;
        m_neighbors[foundIdx].hopCount = hopCount;
        m_neighbors[foundIdx].lastSeen = now;
        memcpy(m_neighbors[foundIdx].routePath, routePath, routePathLen);
        m_neighbors[foundIdx].routePathLen = routePathLen;
        m_neighbors[foundIdx].linkCost = linkCost;
    } else {
        if (m_neighborCount < MAX_NEIGHBORS) {
            memcpy(m_neighbors[m_neighborCount].mac, mac, 6);
            m_neighbors[m_neighborCount].potential = potential;
            m_neighbors[m_neighborCount].hopCount = hopCount;
            m_neighbors[m_neighborCount].lastSeen = now;
            memcpy(m_neighbors[m_neighborCount].routePath, routePath, routePathLen);
            m_neighbors[m_neighborCount].routePathLen = routePathLen;
            m_neighbors[m_neighborCount].linkCost = linkCost;
            m_neighborCount++;
            Serial.printf("[APF Table] Thêm láng giềng mới: %02X:%02X... | RSSI = %d dBm, LinkCost = %.1f\n", 
                          mac[0], mac[1], rssi, linkCost);
        }
    }
}

bool RoutingTable::selectNextHop(float totalPotential) {
    if (m_neighborCount == 0) {
        m_hasRoute = false;
        memset(m_nextHopMac, 0, 6);
        m_myHopCount = 255;
        m_basePotential = 9999.0;
        m_parentRoutePathLen = 0;
        return false;
    }

    float minPotential = 99999.0;
    int minIdx = -1;

    for (int i = 0; i < m_neighborCount; i++) {
        if (pathContainsNode(m_neighbors[i].routePath, m_neighbors[i].routePathLen, m_satelliteId)) {
            continue;
        }
        float effectivePotential = m_neighbors[i].potential + m_neighbors[i].linkCost * 100.0;
        if (effectivePotential < minPotential) {
            minPotential = effectivePotential;
            minIdx = i;
        }
    }

    if (minIdx != -1 && minPotential < totalPotential) {
        memcpy(m_nextHopMac, m_neighbors[minIdx].mac, 6);
        m_myHopCount = m_neighbors[minIdx].hopCount + 1;
        m_basePotential = m_neighbors[minIdx].potential + m_neighbors[minIdx].linkCost * 100.0;
        m_hasRoute = true;

        memcpy(m_parentRoutePath, m_neighbors[minIdx].routePath, m_neighbors[minIdx].routePathLen);
        m_parentRoutePathLen = m_neighbors[minIdx].routePathLen;
        return true;
    }

    m_hasRoute = false;
    memset(m_nextHopMac, 0, 6);
    m_parentRoutePathLen = 0;
    return false;
}

void RoutingTable::removeNeighbor(const uint8_t *mac) {
    for (int i = 0; i < m_neighborCount; i++) {
        if (memcmp(m_neighbors[i].mac, mac, 6) == 0) {
            for (int j = i; j < m_neighborCount - 1; j++) {
                m_neighbors[j] = m_neighbors[j+1];
            }
            m_neighborCount--;
            break;
        }
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
