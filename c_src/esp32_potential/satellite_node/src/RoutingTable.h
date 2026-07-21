#ifndef ROUTING_TABLE_H
#define ROUTING_TABLE_H

#include <Arduino.h>
#include <mesh_packet.h>

#define MAX_NEIGHBORS 10

typedef struct {
    uint8_t mac[6];
    float potential;
    uint8_t hopCount;
    unsigned long lastSeen;
    uint8_t routePath[MAX_ROUTE_PATH];
    uint8_t routePathLen;
    float linkCost;
} Neighbor;

class RoutingTable {
public:
    RoutingTable(uint8_t satelliteId);
    void purgeExpired(unsigned long timeout);
    void updateNeighbor(const uint8_t *mac, float potential, uint8_t hopCount, const uint8_t *routePath, uint8_t routePathLen, int rssi);
    bool selectNextHop(float totalPotential);

    bool hasRoute() const { return m_hasRoute; }
    uint8_t getHopCount() const { return m_myHopCount; }
    float getBasePotential() const { return m_basePotential; }
    const uint8_t* getNextHopMac() const { return m_nextHopMac; }
    
    const uint8_t* getParentRoutePath() const { return m_parentRoutePath; }
    uint8_t getParentRoutePathLen() const { return m_parentRoutePathLen; }

    uint8_t getNeighborCount() const { return m_neighborCount; }
    const Neighbor* getNeighbors() const { return m_neighbors; }
    void removeNeighbor(const uint8_t *mac);
    void setAllowedNeighbors(const uint8_t* allowedList, uint8_t count);
    bool isAllowedNeighbor(uint8_t neighborId) const;

private:
    uint8_t m_satelliteId;
    Neighbor m_neighbors[MAX_NEIGHBORS];
    uint8_t m_neighborCount;

    uint8_t m_nextHopMac[6];
    bool m_hasRoute;
    uint8_t m_myHopCount;
    float m_basePotential;

    uint8_t m_parentRoutePath[MAX_ROUTE_PATH];
    uint8_t m_parentRoutePathLen;

    uint8_t m_allowedNeighbors[10];
    uint8_t m_allowedCount;
};

#endif // ROUTING_TABLE_H
