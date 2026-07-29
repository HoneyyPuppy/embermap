#ifndef ROUTING_TABLE_H
#define ROUTING_TABLE_H

#include <Arduino.h>
#include <mesh_packet.h>

#define MAX_PARENT_CANDIDATES 3
#define MAX_PHYSICAL_NEIGHBORS 3

typedef struct {
    uint8_t mac[6];
    float cost;
    unsigned long lastSeen;
    uint8_t routePath[MAX_ROUTE_PATH];
    uint8_t routePathLen;
} ParentCandidate;

typedef struct {
    uint8_t id;             // ID của láng giềng vật lý (0 = Master/Lối ra, 1-3 = Vệ tinh)
    uint8_t mac[6];         // Địa chỉ MAC học được của láng giềng
    float distance;         // Khoảng cách đi bộ (mét)
    float evacPotential;    // Thế năng thoát hiểm nhận từ láng giềng
    uint8_t evacNextHopId;  // ID của nút tiếp theo trên lối thoát hiểm của láng giềng
    unsigned long lastSeen; // Thời điểm nhận cập nhật cuối
    bool active;            // Trạng thái đã học được MAC hay chưa
} PhysicalNeighbor;

#define MAX_LINK_QUALITY_ENTRIES 6

typedef struct {
    uint8_t mac[6];
    float etx;
    float deliveryRatio;
    unsigned long lastTxTime;
    bool active;
} LinkQuality;

class RoutingTable {
public:
    RoutingTable(uint8_t satelliteId);
    void purgeExpired(unsigned long timeout);
    void updateCandidate(const uint8_t *mac, float cost, const uint8_t *routePath, uint8_t routePathLen);
    
    bool hasRoute() const { return m_hasRoute; }
    float getCost() const { return m_myCost; }
    const uint8_t* getNextHopMac() const { return m_nextHopMac; }
    uint8_t getParentCount() const { return m_parentCount; }
    const ParentCandidate* getCandidates() const { return m_candidates; }
    uint8_t getDataNextHopId() const;
    
    const uint8_t* getParentRoutePath() const { return m_parentRoutePath; }
    uint8_t getParentRoutePathLen() const { return m_parentRoutePathLen; }

    void removeCandidateAtIndex(uint8_t idx);
    void clearCandidates();
    void setAllowedNeighbors(const uint8_t* allowedList, uint8_t count);
    bool isAllowedNeighbor(uint8_t neighborId) const;

    // Các hàm phục vụ chỉ số chất lượng liên kết ETX
    void updateEtx(const uint8_t* mac, bool success);
    float getLinkEtx(const uint8_t* mac) const;
    void recordTxTime(const uint8_t* mac);
    unsigned long getLastTxTime(const uint8_t* mac) const;

    // Các hàm phục vụ định tuyến thoát hiểm con người (Evacuation Routing)
    void setPhysicalNeighbors(const uint8_t* ids, const float* distances, uint8_t count);
    void updatePhysicalNeighborMac(uint8_t id, const uint8_t* mac);
    bool updateEvacPotential(uint8_t neighborId, float potential, uint8_t nextHopId);
    bool calculateEvacuation(float localRepulsive, float &outTotalPotential, uint8_t &outNextHopId, uint8_t *outNextHopMac);
    float getMyEvacPotential() const { return m_myEvacPotential; }
    uint8_t getEvacNextHopId() const { return m_evacNextHopId; }
    const PhysicalNeighbor* getPhysicalNeighbors() const { return m_physNeighbors; }
    uint8_t getPhysCount() const { return m_physCount; }

private:
    LinkQuality m_linkQualities[MAX_LINK_QUALITY_ENTRIES];
    uint8_t m_satelliteId;
    ParentCandidate m_candidates[MAX_PARENT_CANDIDATES];
    uint8_t m_parentCount;

    uint8_t m_nextHopMac[6];
    float m_myCost;
    bool m_hasRoute;

    uint8_t m_parentRoutePath[MAX_ROUTE_PATH];
    uint8_t m_parentRoutePathLen;

    uint8_t m_allowedNeighbors[10];
    uint8_t m_allowedCount;

    // Dữ liệu chỉ đường thoát hiểm
    PhysicalNeighbor m_physNeighbors[MAX_PHYSICAL_NEIGHBORS];
    uint8_t m_physCount;
    float m_myEvacPotential;
    uint8_t m_evacNextHopId;
    uint8_t m_evacNextHopMac[6];

    void sortCandidates();
    void syncPrimary();
};

#endif // ROUTING_TABLE_H
