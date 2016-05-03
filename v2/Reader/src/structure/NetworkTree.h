/*
 * NetworkTree.h
 *
 *  Created on: Nov 16, 2014
 *      Author: grailet
 *
 * NetworkTree is a custom kind of tree that models the DAG (directed acyclic graph) made of the
 * previousfly inferred (and refined) subnets where the leaves are the subnets and the internal
 * nodes are the neighborhoods (i.e. network areas where every device is reachable with at most 
 * one hop). Each node is labeled with an interface which is the responding interface when 
 * sending an ICMP probe to some target subnet with a smaller TTL than required. In other words,
 * for a given subnet located at N hops, one will have to go through N internal nodes in the
 * tree to reach the subnet.
 *
 * While what we obtain with routes can usually be viewed as a tree, it is still possible to 
 * have a DAG because of load balancing (i.e. branches reaching a same point but using different 
 * routes). Construction method ensures that routes bearing differences for a same destination 
 * (because of load balancing, for example) are fused into a single branch (January 2015).
 *
 * The main goal of building such tree is to locate subnets between themselves and to have a good
 * approximation of the number of interfaces bordering a neighborhood (which is necessary for 
 * L2/L3 devices inference).
 */

#ifndef NETWORKTREE_H_
#define NETWORKTREE_H_

#include "./NetworkTreeNode.h"
#include "../aliasresolution/AliasHintCollector.h"
#include "../aliasresolution/AliasResolver.h"
#include "../bipartite/BipartiteGraph.h"

class NetworkTree
{
public:

    /*
     * Size of the subnetMap array (array of lists), which is used for fast look-up of a subnet 
     * stored in the tree on the basis of an interface it should contain. Its size is base on the 
     * fact that no subnet of a prefix length shorter than /20 was ever found in the measurements. 
     * Therefore, the 20 first bits of any interface in a subnet are used to access a list of 
     * subnets sharing the same prefix in O(1), the list containing at most 2048 subnets (2048 
     * subnets of prefix length /31). This dramatically speeds up the look-up for a subnet stored 
     * in the tree (in comparison with a more trivial method where one visits the tree), at the 
     * cost of using more memory).
     */

    const static int SIZE_SUBNET_MAP = 1048576;

    /*
     * About maxDepth parameter: it is the size of the longest route to a subnet which should be 
     * inserted in the tree. It is used as the size of the depthMap array (i.e. one list of nodes 
     * per depth level), which should be maintained throughout the life of the tree to ease the 
     * insertion step (re-building the whole map at each insertion is costly).
     */

    NetworkTree(unsigned short maxDepth);
    ~NetworkTree();
    
    // Insertion method
    void insert(SubnetSite *subnet);
    
    // Prints the tree (policy: depth-first pre-order)
    void visit(ostream *out);
    
    /*
     * Gets statistics over the tree in an array made of the following cells:
     * -[0]: amount of neighborhoods
     * -[1]: amount of neighborhoods with only subnets as children
     * -[2]: amount of neighborhoods with complete linkage
     * -[3]: amount of neighborhoods with complete or partial linkage (1 or 2 missing links)
      * -[4]: amount of neighborhoods which all labels appear in measured subnets
     */
    
    unsigned int *getStatistics();
    
    // Completes the route to a given subnet with information present in the tree.
    void repairRoute(SubnetSite *ss);
    
    // Visits each internal node to collect alias resolution hints
    void collectAliasResolutionHints(ostream *out, AliasHintCollector *ahc);
    
    // Gets a subnet contained in the tree which contains the given input address.
    SubnetSite *getSubnetContaining(InetAddress needle);
    
    // Computes the router list for each node when possible
    void inferRouters(AliasResolver *ar);
   
    // Gets the size of the largest fingerprint list (to call AFTER inferRouters()).
    unsigned int largestFingerprintList();
    
    // Prints the interesting internal nodes (i.e. more than one child that is not an internal)
    void internals(ostream *out);
    
    // Method to write the leaves/inferred routers in an output file of a given name.
    void outputSubnets(string filename);
    void outputAliases(string filename);
    
    // All methods below this point are unique to TreeNET Reader.
    
    /*
     * Methods to evaluate the size of the main trunk (i.e., successions of nodes starting from 
     * the root with a single child each time), the presence of holes (i.e., 0.0.0.0) and list all 
     * interfaces occurring beyond that trunk.
     */
    
    unsigned short getTrunkSize();
    bool hasIncompleteTrunk();
    list<InetAddress> listInterfacesAfterTrunk();
    
    /*
     * Method to set pointers to subnets in leaves to NULL (for deletion); those pointers are 
     * however put back in a SubnetSiteSet object if sink (pointer to that object) is not NULL.
     */
    
    void nullifyLeaves(SubnetSiteSet *sink);
    
    /*
     * Method to see if a given subnet has a route that can fit in the tree, using the labels of 
     * the main trunk as reference.
     */
    
    bool fittingRoute(SubnetSite *ss);
    
    /*
     * Method to study a possible transplantation of the route to a given subnet to fit in the 
     * tree.
     *
     * @param SubnetSite*     ss         The subnet to insert
     * @param unsigned short* sOld       The size of the array "oldPrefix" (see below)
     * @param InetAddress**   oldPrefix  The route prefix that should be replaced
     * @param unsigned short* sNew       The size of the array "newPrefix" (see below)
     * @param InetAddress**   newPrefix  The route prefix that should replace "oldPrefix"
     * @return bool                      True if a transplantation has been found, false otherwise
     */
    
    bool findTransplantation(SubnetSite *ss, 
                             unsigned short *sOld, 
                             InetAddress **oldPrefix, 
                             unsigned short *sNew, 
                             InetAddress **newPrefix);
    
    // Method to generate a bipartite graph from the tree.
    BipartiteGraph *toBipartite();
    
private:

    /*
     * About depth parameter (insertSubnet() and insertRecursive()): it is the depth at which we
     * are in the tree, which we need to know in order to find the appropriate label(s) in the
     * subnet route array.
     */

    // Method to actually insert a subnet (with a cascade of internal nodes if needed)
    static NetworkTreeNode *insertSubnet(SubnetSite *subnet, unsigned short depth);
    
    // Recursive methods to go through the tree for the methods of the (more or less) same name
    static void insertRecursive(SubnetSite *subnet, NetworkTreeNode *cur, unsigned short depth);
    static void visitRecursive(ostream *out, NetworkTreeNode *cur, unsigned short depth);
    static void statisticsRecursive(unsigned int *stat, NetworkTree *tree, NetworkTreeNode *cur);
    static void collectHintsRecursive(ostream *out, 
                                      AliasHintCollector *ahc, 
                                      NetworkTreeNode *cur, 
                                      unsigned short depth);
    static void listSubnetsRecursive(list<SubnetSite*> *subnetsList, NetworkTreeNode *cur);
    static void outputAliasesRecursive(NetworkTreeNode *cur, string *aliasesStr);
    static void largestListRecursive(NetworkTreeNode *cur, unsigned int *largest);
    static void inferRoutersRecursive(NetworkTree *tree, 
                                      NetworkTreeNode *cur,  
                                      AliasResolver *ar, 
                                      unsigned short depth);
    static void internalsRecursive(ostream *out, NetworkTree *tree, NetworkTreeNode *cur);
    static void listInterfacesRecursive(list<InetAddress> *interfaces, NetworkTreeNode *cur);
    static void nullifyLeavesRecursive(SubnetSiteSet *sink, NetworkTreeNode *cur);
    
    /*
     * Recursive method (but going back up in the tree) to prune a branch which last node has
     * no leaf and is not a T_SUBNET node and which all intermediate nodes have a single child.
     * The depth map (map parameter) and the depth metric are necessary, because we have to 
     * remove each deleted node from the depth map as well.
     */
    
    static void prune(list<NetworkTreeNode*> *map, 
                      NetworkTreeNode *cur, 
                      NetworkTreeNode *prev,
                      unsigned short depth);
    
    /*
     * Methods involved in the construction of the bipartite equivalent of a tree. In addition 
     * to a recursive method traveling across the tree, several methods gathers operations that 
     * occur several times in the bipartite conversion in order to simplify the code.
     */
    
    static void toBipartiteRecursive(BipartiteGraph *bip, 
                                     NetworkTree *tree, 
                                     NetworkTreeNode *cur,
                                     unsigned short depth);
    
    /*
     * Connects a given router (ingressRouter) with the subnets from childrenS it gives access to.
     */
    
    static void bipConnectWithSubnets(BipartiteGraph *bip,
                                      Router *ingressRouter,
                                      list<SubnetSite*> *childrenS);
    
    /*
     * Connects a given router (ingressRouter) with the internal nodes it should give access to
     * (found in childrenN). childrenS is necessary to see which subnet is crossed to reach an 
     * internal.
     */
    
    static void bipConnectWithInternals(BipartiteGraph *bip, 
                                        NetworkTree *tree, 
                                        BipartiteRouter *ingressRouter,
                                        list<SubnetSite*> *childrenS,
                                        list<NetworkTreeNode*> *childrenN);
    
    /*
     * Connects a given router with a given internal. The difference with the previous function 
     * is that this one cares about a single link, while the previous cares about multiple links,
     * possibly multiple links with a same load balancing child internal node. In case of load 
     * balancing (childN), the label for which the link is created is explicitely given.
     */
    
    static void bipConnectWithInternal(BipartiteGraph *bip,
                                       NetworkTree *tree,
                                       BipartiteRouter *ingressRouter,
                                       list<SubnetSite*> *childrenS,
                                       NetworkTreeNode *childN,
                                       InetAddress labelChild,
                                       Router *ingressRouterChild);

    /*
     * Private fields: root of the tree, depth map (for insertion), subnet map (for look up) along 
     * its size.
     */
    
    NetworkTreeNode *root;
    list<NetworkTreeNode*> *depthMap;
    list<SubnetSite*> *subnetMap;
    unsigned short maxDepth;
};

#endif /* NETWORKTREE_H_ */
