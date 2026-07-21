#ifndef ROUTING_TABLE_H
#define ROUTING_TABLE_H

#include <Arduino.h>
#include <mesh_packet.h>

#define MAX_PARENT_CANDIDATES 3

typedef struct {
    uint8_t mac[6];
    uint16_t rank;
    unsigned long lastSeen;
    uint8_t routePath[MAX_ROUTE_PATH];
    uint8_t routePathLen;
} ParentCandidate;

class RoutingTable {
public:
    RoutingTable(uint8_t satelliteId);
    void purgeExpired(unsigned long timeout);
    void updateCandidate(const uint8_t *mac, uint16_t rank, uint8_t version, const uint8_t *routePath, uint8_t routePathLen);
    
    bool hasParent() const { return m_hasParent; }
    uint16_t getRank() const { return m_myRank; }
    uint8_t getDodagVersion() const { return m_dodagVersion; }
    const uint8_t* getParentMac() const { return m_parentMac; }
    uint8_t getParentCount() const { return m_parentCount; }
    const ParentCandidate* getCandidates() const { return m_candidates; }
    
    const uint8_t* getParentRoutePath() const { return m_parentRoutePath; }
    uint8_t getParentRoutePathLen() const { return m_parentRoutePathLen; }

    void removeCandidateAtIndex(uint8_t idx);
    void setAllowedNeighbors(const uint8_t* allowedList, uint8_t count);
    bool isAllowedNeighbor(uint8_t neighborId) const;

private:
    uint8_t m_satelliteId;
    ParentCandidate m_candidates[MAX_PARENT_CANDIDATES];
    uint8_t m_parentCount;

    uint8_t m_parentMac[6];
    uint16_t m_myRank;
    uint8_t m_dodagVersion;
    bool m_hasParent;

    uint8_t m_parentRoutePath[MAX_ROUTE_PATH];
    uint8_t m_parentRoutePathLen;

    uint8_t m_allowedNeighbors[10];
    uint8_t m_allowedCount;

    void sortCandidates();
    void syncPrimary();
};

#endif // ROUTING_TABLE_H
