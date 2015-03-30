/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
//
// Copyright (c) 2006 Georgia Tech Research Corporation
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Author: Rajib Bhattacharjea<raj.b@gatech.edu>
//


// Ported from:
// Georgia Tech Network Simulator - Round Trip Time Estimation Class
// George F. Riley.  Georgia Tech, Spring 2002

// Implements several variations of round trip time estimators

#include <iostream>
#include <deque>

#include "rtt-estimator.h"
#include "ns3/simulator.h"
#include "ns3/double.h"


#include "ns3/log.h"

namespace ns3{

NS_LOG_COMPONENT_DEFINE("RttEstimator");


NS_OBJECT_ENSURE_REGISTERED (RttEstimator);

//RttEstimator iid
TypeId
RttEstimator::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::RttEstimator")
    .SetParent<Object> ()
    .AddAttribute ("MaxMultiplier",
                   "XXX",
                   DoubleValue (64.0),
                   MakeDoubleAccessor (&RttEstimator::m_maxMultiplier),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("InitialEstimation",
                   "XXX",
                   TimeValue (Seconds (1.0)),
                   MakeTimeAccessor (&RttEstimator::est),
                   MakeTimeChecker ())
    .AddAttribute ("MinRTO",
                   "Minimum retransmit timeout value",
                   TimeValue (Seconds (0.2)),
                   MakeTimeAccessor (&RttEstimator::minrto),
                   MakeTimeChecker ())
    ;
  return tid;
}

//RttHistory methods
RttHistory::RttHistory (SequenceNumber s, uint32_t c, Time t)
  : seq (s), count (c), time (t), retx (false)
  {
  }

RttHistory::RttHistory (const RttHistory& h)
  : seq (h.seq), count (h.count), time (h.time), retx (h.retx)
  {
  }

// Base class methods

RttEstimator::RttEstimator () : next (1), history (),
    m_maxMultiplier (64.0), nSamples (0), multiplier (1.0)
{
  //note next=1 everywhere since first segment will have sequence 1
}

RttEstimator::RttEstimator(const RttEstimator& c)
  : Object (c), next(c.next), history(c.history),
    m_maxMultiplier (c.m_maxMultiplier), est(c.est), nSamples(c.nSamples),
    multiplier(c.multiplier)
{}

RttEstimator::~RttEstimator ()
{
}

void RttEstimator::SentSeq (SequenceNumber s, uint32_t c)
{ // Note that a particular sequence has been sent
  NS_LOG_FUNCTION(this << (uint32_t)s << c);
  if (s == next)
    { // This is the next expected one, just log at end
      NS_LOG_INFO("RttEstimator::SentSeq -> s == next == "<< (uint32_t) s);
      history.push_back (RttHistory (s, c, Simulator::Now () ));
      next = s + SequenceNumber (c); // Update next expected
    }
  else
    { // This is a retransmit, find in list and mark as re-tx
      NS_LOG_INFO("RttEstimator::SentSeq -> s ("<< (uint32_t)s <<") != next ("<< next <<")");
      for (RttHistory_t::iterator i = history.begin (); i != history.end (); ++i)
        {
          if ((s >= i->seq) && (s < (i->seq + SequenceNumber (i->count))))
            { // Found it
              NS_LOG_INFO("RttEstimator::SentSeq -> the seq number have been fond");
              i->retx = true;
              // One final test..be sure this re-tx does not extend "next"
              if ((s + SequenceNumber (c)) > next)
                {
                  next = s + SequenceNumber (c);
                  i->count = ((s + SequenceNumber (c)) - i->seq); // And update count in hist
                }
              break;
            }
        }
    }
}

Time RttEstimator::AckSeq (SequenceNumber a)
{ // An ack has been received, calculate rtt and log this measurement
  // Note we use a linear search (O(n)) for this since for the common
  // case the ack'ed packet will be at the head of the list
  NS_LOG_FUNCTION(this << a);
  Time m = Seconds (0.0);
  if (history.size () == 0) return (m);    // No pending history, just exit
  RttHistory& h = history.front ();
  if (!h.retx && a >= (h.seq + SequenceNumber (h.count)))
    { // Ok to use this sample
        NS_LOG_INFO ("Before Measurement(elapsedTime) -> est = " << est.GetSeconds ());
        m = Simulator::Now () - h.time; // Elapsed time
        Measurement(m);                // Log the measurement
        ResetMultiplier();             // Reset multiplier on valid measurement
        NS_LOG_INFO ("After  Measurement(elapsedTime) -> est = " << est.GetSeconds ());
    }
  // Now delete all ack history with seq <= ack
  while(history.size() > 0)
    {
        RttHistory& h = history.front ();
        if ((h.seq + SequenceNumber(h.count)) > a) break;                // Done removing
        history.pop_front (); // Remove
    }
  return m;
}

void RttEstimator::ClearSent ()
{ // Clear all history entries
  next = 1;
  history.clear ();
}

void RttEstimator::IncreaseMultiplier ()
{
  double a;
  a = multiplier * 2.0;
  double b;
  b = m_maxMultiplier * 2.0;
  multiplier = std::min (multiplier * 2.0, m_maxMultiplier);
}

void RttEstimator::ResetMultiplier ()
{
  multiplier = 1.0;
}

void RttEstimator::Reset ()
{ // Reset to initial state
  next = 1;
  est  = Seconds (1.0); // XXX: we should go back to the 'initial value' here. Need to add support in Object for this.
  history.clear ();         // Remove all info from the history
  nSamples = 0;
  ResetMultiplier ();
}

void
RttEstimator::pktRetransmit (SequenceNumber seq)
{
    std::deque<RttHistory>::iterator current = history.begin();
    std::deque<RttHistory>::iterator next = history.begin();
    while(next != history.end())
    {
        current = next;
        if ((*current).seq == seq)
        {
            history.erase(current);
            /**
             * because the corresponding packet have been retransmitted we preffer to not take into acount its RTT because we can't
             * differenciate between the case where the received ack is for the first segment or the retransmitted one
             */
            //(*it).time = Simulator::Now();
            break;
        }
        ++next;
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Mean-Deviation Estimator

NS_OBJECT_ENSURE_REGISTERED (RttMeanDeviation);

TypeId
RttMeanDeviation::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::RttMeanDeviation")
    .SetParent<RttEstimator> ()
    .AddConstructor<RttMeanDeviation> ()
    .AddAttribute ("Gain",
                   "XXX",
                   DoubleValue (0.1),
                   MakeDoubleAccessor (&RttMeanDeviation::gain),
                   MakeDoubleChecker<double> ())
    ;
  return tid;
}

RttMeanDeviation::RttMeanDeviation() :
  variance (ns3::Seconds(0))
{
}

RttMeanDeviation::RttMeanDeviation (const RttMeanDeviation& c)
  : RttEstimator (c), gain (c.gain), variance (c.variance)
{
}

void RttMeanDeviation::Measurement (Time m)
{
    NS_LOG_FUNCTION(this << m);
    NS_LOG_INFO("gain == " << gain);
  if (nSamples)
    { // Not first

      Time err = m - est;
      est = est + Scalar (gain) * err;         // estimated rtt
      err = Abs (err);        // absolute value of error
      variance = variance + Scalar (gain) * (err - variance); // variance of rtt
      NS_LOG_INFO("nSamples != 0");
      NS_LOG_INFO("est == "<< est);
    }
  else
    { // First sample
      est = m;                        // Set estimate to current
      //variance = m / 2;               // And variance to current / 2
      variance = m; // try this
      NS_LOG_INFO("nSamples == 0");
    }
  nSamples++;
}

Time RttMeanDeviation::RetransmitTimeout ()
{
  // If not enough samples, justjust return 2 times estimate
  //if (nSamples < 2) return est * 2;
  Time retval;

  if (variance < est / Scalar (4.0))
    {
      retval = est * Scalar (2 * multiplier);            // At least twice current est
    }
  else
    {
      retval = (est + Scalar (4) * variance) * Scalar (multiplier); // As suggested by Jacobson
    }
  NS_LOG_INFO ("RetransmitTimeout -> est ("<< est.GetSeconds() <<") variance ("<< variance.GetSeconds() <<") retval ("<< retval.GetSeconds() <<") minrto ("<< minrto <<") multiplier = "<< multiplier);
  retval = Max (retval, minrto);
  return retval;
}

Ptr<RttEstimator> RttMeanDeviation::Copy () const
{
  return CopyObject<RttMeanDeviation> (this);
}

void RttMeanDeviation::Reset ()
{ // Reset to initial state
  variance = Seconds (0.0);
  RttEstimator::Reset ();
}
}//namepsace ns3
