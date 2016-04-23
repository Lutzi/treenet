/*
 * AliasHintCollector.cpp
 *
 *  Created on: Feb 27, 2015
 *      Author: grailet
 *
 * Implements the class defined in AliasHintCollector.h (see this file to learn further about the 
 * goals of such class).
 */

#include "AliasHintCollector.h"
#include "IPIDCollector.h"
#include "UDPUnreachablePortUnit.h"
#include "TimestampCheckUnit.h"
#include "ReverseDNSUnit.h"
#include "../common/thread/Thread.h"

AliasHintCollector::AliasHintCollector(TreeNETEnvironment *env)
{
    this->env = env;
    tokenCounter = 1;
}

AliasHintCollector::~AliasHintCollector()
{
}

void AliasHintCollector::collect()
{
    IPLookUpTable *table = env->getIPTable();
    ostream *out = env->getOutputStream();
    
    /*
     * Sorts and removes duplicata (possible because ingress interface of neighborhood can be a 
     * contra-pivot). Also inserts the IPs that are missing from the IP table.
     */
    
    this->IPsToProbe.sort(InetAddress::smaller);
    InetAddress previous(0);
    for(list<InetAddress>::iterator i = this->IPsToProbe.begin(); i != this->IPsToProbe.end(); ++i)
    {
        InetAddress current = (*i);
        
        if(current == previous)
        {
            this->IPsToProbe.erase(i--);
        }
        else
        {
            if(table->lookUp(current) == NULL)
            {
                IPTableEntry *newEntry = table->create(current);
                newEntry->setTTL(this->currentTTL);
            }
        }
        
        previous = current;
    }
    
    // Amounts of threads and collected IP-IDs
    unsigned short maxThreads = env->getMaxThreads();
    unsigned short nbIPIDs = env->getNbIPIDs();
    unsigned long int nbIPs = (unsigned long int) this->IPsToProbe.size();
    if(nbIPs == 0)
    {
        return;
    }
    unsigned short nbThreads = 1;
    
    // Computes the amount of required threads (+1 is due to collector thread itself)
    unsigned short maxCollectors = maxThreads / (nbIPIDs + 1);
    if(nbIPs > (unsigned long int) maxCollectors)
        nbThreads = maxCollectors;
    else
        nbThreads = (unsigned short) nbIPs;
    
    // Initializes an array of threads
    Thread **th = new Thread*[nbThreads];
    for(unsigned short i = 0; i < nbThreads; i++)
        th[i] = NULL;

    // Does a copy of the IPs list for next hints
    list<InetAddress> backUp1(this->IPsToProbe);
    list<InetAddress> backUp2(this->IPsToProbe);
    list<InetAddress> backUp3(this->IPsToProbe);

    (*out) << "1. IP-ID collection... " << std::flush;

    // Starts scheduling for IP-ID retrieval
    unsigned short j = 0;
    for(unsigned long int i = 0; i < nbIPs; i++)
    {
        InetAddress IPToProbe(this->IPsToProbe.front());
        this->IPsToProbe.pop_front();
        
        if(th[j] != NULL)
        {
            th[j]->join();
            delete th[j];
            th[j] = NULL;
        }
        
        th[j] = new Thread(new IPIDCollector(env, this, IPToProbe, j * nbIPIDs));
        th[j]->start();
        
        j++;
        if(j == nbThreads)
            j = 0;
        
        // 0,01s of delay before next thread
        Thread::invokeSleep(TimeVal(0, 10000));
    }

    // Waiting for all remaining threads to complete
    for(unsigned short i = 0; i < nbThreads; i++)
    {
        if(th[i] != NULL)
        {
            th[i]->join();
            delete th[i];
            th[i] = NULL;
        }
    }
    
    delete[] th;
    
    (*out) << "done." << endl;
    
    // Re-sizes th[] for the other hints (because each time, there is a single thread per IP)
    if(nbIPs > (unsigned long int) maxThreads)
        nbThreads = maxThreads;
    else
        nbThreads = (unsigned short) nbIPs;
    
    th = new Thread*[nbThreads];
    for(unsigned short i = 0; i < nbThreads; i++)
        th[i] = NULL;
    
    // Now schedules threads to try the UDP/ICMP Port Unreachable approach
    (*out) << "2. Probing each IP with UDP (unreachable port)... " << std::flush;
    
    unsigned short range = (DirectProber::DEFAULT_UPPER_SRC_PORT_ICMP_ID - DirectProber::DEFAULT_LOWER_SRC_PORT_ICMP_ID) / maxThreads;
    j = 0;
    for(unsigned long int i = 0; i < nbIPs; i++)
    {
        InetAddress IPToProbe = backUp1.front();
        backUp1.pop_front();
        
        if(th[j] != NULL)
        {
            th[j]->join();
            delete th[j];
            th[j] = NULL;
        }
        
        th[j] = new Thread(new UDPUnreachablePortUnit(env, 
                                                      IPToProbe, 
                                                      DirectProber::DEFAULT_LOWER_SRC_PORT_ICMP_ID + (j * range), 
                                                      DirectProber::DEFAULT_LOWER_SRC_PORT_ICMP_ID + (j * range) + range - 1, 
                                                      DirectProber::DEFAULT_LOWER_DST_PORT_ICMP_SEQ, 
                                                      DirectProber::DEFAULT_UPPER_DST_PORT_ICMP_SEQ));
        th[j]->start();
        
        j++;
        if(j == maxThreads)
            j = 0;
        
        // 0,1s of delay before next thread (if same router, avoids to "bomb" it)
        Thread::invokeSleep(TimeVal(0, 100000));
    }
    
    // Waiting for all remaining threads to complete
    for(unsigned short i = 0; i < nbThreads; i++)
    {
        if(th[i] != NULL)
        {
            th[i]->join();
            delete th[i];
            th[i] = NULL;
        }
    }
    
    (*out) << "done." << endl;
    
    // Now schedules to check if each IP is compatible with prespecified timestamp option...
    (*out) << "3. Sending ICMP timestamp request to each IP... " << std::flush;
    
    j = 0;
    for(unsigned long int i = 0; i < nbIPs; i++)
    {
        InetAddress IPToProbe = backUp2.front();
        backUp2.pop_front();
        
        if(th[j] != NULL)
        {
            th[j]->join();
            delete th[j];
            th[j] = NULL;
        }
        
        th[j] = new Thread(new TimestampCheckUnit(env, 
                                                  IPToProbe, 
                                                  DirectProber::DEFAULT_LOWER_SRC_PORT_ICMP_ID + (j * range), 
                                                  DirectProber::DEFAULT_LOWER_SRC_PORT_ICMP_ID + (j * range) + range - 1, 
                                                  DirectProber::DEFAULT_LOWER_DST_PORT_ICMP_SEQ, 
                                                  DirectProber::DEFAULT_UPPER_DST_PORT_ICMP_SEQ));
        th[j]->start();
        
        j++;
        if(j == maxThreads)
            j = 0;
        
        // 0,1s of delay before next thread (if same router, avoids to "bomb" it)
        Thread::invokeSleep(TimeVal(0, 100000));
    }
    
    // Waiting for all remaining threads to complete
    for(unsigned short i = 0; i < nbThreads; i++)
    {
        if(th[i] != NULL)
        {
            th[i]->join();
            delete th[i];
            th[i] = NULL;
        }
    }
    
    (*out) << "done." << endl;
    
    // Now schedules to resolve host names of each IP by reverse DNS
    (*out) << "4. Reverse DNS... " << std::flush;
    
    j = 0;
    for(unsigned long int i = 0; i < nbIPs; i++)
    {
        InetAddress IPToProbe = backUp3.front();
        backUp3.pop_front();
        
        if(th[j] != NULL)
        {
            th[j]->join();
            delete th[j];
            th[j] = NULL;
        }
        
        th[j] = new Thread(new ReverseDNSUnit(env, IPToProbe));
        th[j]->start();
        
        j++;
        if(j == maxThreads)
            j = 0;
        
        // 0,01s of delay before next thread
        Thread::invokeSleep(TimeVal(0, 10000));
    }
    
    // Waiting for all remaining threads to complete
    for(unsigned short i = 0; i < nbThreads; i++)
    {
        if(th[i] != NULL)
        {
            th[i]->join();
            delete th[i];
            th[i] = NULL;
        }
    }
    
    delete[] th;
    
    (*out) << "done." << endl;
}

unsigned long int AliasHintCollector::getProbeToken()
{
    unsigned long int token = tokenCounter;
    tokenCounter++;
    return token;
}
