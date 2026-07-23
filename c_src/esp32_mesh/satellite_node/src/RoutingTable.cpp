#include "RoutingTable.h"

RoutingTable::RoutingTable(uint8_t satelliteId)
    : m_satelliteId(satelliteId), m_parentCount(0), m_myCost(999.0), m_hasRoute(false), m_parentRoutePathLen(0), m_allowedCount(0),
      m_physCount(0), m_myEvacPotential(9999.0), m_evacNextHopId(0xFF) {
    memset(m_nextHopMac, 0, 6);
    memset(m_evacNextHopMac, 0, 6);
    memset(m_physNeighbors, 0, sizeof(m_physNeighbors));
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

void RoutingTable::clearCandidates() {
    m_parentCount = 0;
    syncPrimary();
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

void RoutingTable::setPhysicalNeighbors(const uint8_t* ids, const float* distances, uint8_t count) {
    m_physCount = count > MAX_PHYSICAL_NEIGHBORS ? MAX_PHYSICAL_NEIGHBORS : count;
    for (uint8_t i = 0; i < m_physCount; i++) {
        m_physNeighbors[i].id = ids[i];
        m_physNeighbors[i].distance = distances[i];
        m_physNeighbors[i].evacPotential = ids[i] == 0 ? 0.0 : 9999.0;
        m_physNeighbors[i].active = ids[i] == 0 ? true : false;
        m_physNeighbors[i].lastSeen = millis();
        memset(m_physNeighbors[i].mac, 0, 6);
    }
}

void RoutingTable::updatePhysicalNeighborMac(uint8_t id, const uint8_t* mac) {
    for (uint8_t i = 0; i < m_physCount; i++) {
        if (m_physNeighbors[i].id == id) {
            memcpy(m_physNeighbors[i].mac, mac, 6);
            m_physNeighbors[i].active = true;
            m_physNeighbors[i].lastSeen = millis();
            break;
        }
    }
}

void RoutingTable::updateEvacPotential(uint8_t neighborId, float potential) {
    for (uint8_t i = 0; i < m_physCount; i++) {
        if (m_physNeighbors[i].id == neighborId) {
            m_physNeighbors[i].evacPotential = potential;
            m_physNeighbors[i].lastSeen = millis();
            m_physNeighbors[i].active = true;
            break;
        }
    }
}

bool RoutingTable::calculateEvacuation(float localRepulsive, float &outTotalPotential, uint8_t &outNextHopId, uint8_t *outNextHopMac) {
    if (localRepulsive >= 9999.0) {
        m_myEvacPotential = 9999.0;
        m_evacNextHopId = 0xFF;
        memset(m_evacNextHopMac, 0, 6);
        outTotalPotential = 9999.0;
        outNextHopId = 0xFF;
        memset(outNextHopMac, 0, 6);
        return false;
    }

    float minPotential = 99999.0;
    int minIdx = -1;

    for (uint8_t i = 0; i < m_physCount; i++) {
        if (m_physNeighbors[i].active || m_physNeighbors[i].id == 0) {
            if (m_physNeighbors[i].id != 0 && (millis() - m_physNeighbors[i].lastSeen > 15000)) {
                m_physNeighbors[i].evacPotential = 9999.0;
            }

            float candidatePotential = m_physNeighbors[i].evacPotential + m_physNeighbors[i].distance;
            Serial.printf("[Evac Calc] Xét láng giềng ID=%d: U_neighbor=%.1f + dist=%.1f = %.1f (active=%d)\n",
                          m_physNeighbors[i].id, m_physNeighbors[i].evacPotential, 
                          m_physNeighbors[i].distance, candidatePotential, m_physNeighbors[i].active);
            if (candidatePotential < minPotential) {
                minPotential = candidatePotential;
                minIdx = i;
            }
        }
    }

    if (minIdx != -1 && minPotential < 9999.0) {
        m_myEvacPotential = minPotential + localRepulsive;
        if (m_myEvacPotential > 9999.0) m_myEvacPotential = 9999.0;

        m_evacNextHopId = m_physNeighbors[minIdx].id;
        memcpy(m_evacNextHopMac, m_physNeighbors[minIdx].mac, 6);
        
        outTotalPotential = m_myEvacPotential;
        outNextHopId = m_evacNextHopId;
        memcpy(outNextHopMac, m_evacNextHopMac, 6);
        return true;
    } else {
        m_myEvacPotential = 9999.0;
        m_evacNextHopId = 0xFF;
        memset(m_evacNextHopMac, 0, 6);
        
        outTotalPotential = 9999.0;
        outNextHopId = 0xFF;
        memset(outNextHopMac, 0, 6);
        return false;
    }
}
