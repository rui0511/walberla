//======================================================================================================================
//
//  This file is part of waLBerla. waLBerla is free software: you can 
//  redistribute it and/or modify it under the terms of the GNU General Public
//  License as published by the Free Software Foundation, either version 3 of 
//  the License, or (at your option) any later version.
//  
//  waLBerla is distributed in the hope that it will be useful, but WITHOUT 
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
//  for more details.
//  
//  You should have received a copy of the GNU General Public License along
//  with waLBerla (see COPYING.txt). If not, see <http://www.gnu.org/licenses/>.
//
//! \file BufferSystemHelper.cpp
//! \ingroup core
//! \author Martin Bauer <martin.bauer@fau.de>
//
//======================================================================================================================

#include "BufferSystemHelper.h"
#include "MPIManager.h"

#include "core/Abort.h"
#include "core/debug/Debug.h"
#include "core/logging/Logging.h"


namespace walberla {
namespace mpi {
namespace internal {

//======================================================================================================================
//
//  KnownSizeCommunication
//
//======================================================================================================================


void KnownSizeCommunication::send( MPIRank receiver, const SendBuffer & sendBuffer )
{
   WALBERLA_NON_MPI_SECTION() { WALBERLA_ASSERT( false ); }

   if ( ! sending_ )
      sending_ = true;

   sendRequests_.push_back( MPI_REQUEST_NULL );
   MPI_Request & request = sendRequests_.back();
   MPI_Isend( sendBuffer.ptr(),          // pointer to size buffer
              int_c(sendBuffer.size() ), // send one size
              MPI_BYTE,                  // type
              receiver,                  // receiver rank
              tag_,                      // message tag
              communicator_,             // communicator
              &request                   // request needed for wait
              );
}



void KnownSizeCommunication::waitForSends( )
{
   WALBERLA_NON_MPI_SECTION() { WALBERLA_ASSERT( false ); }

   sending_ = false;

   if ( sendRequests_.empty() )
      return;

   MPI_Waitall( int_c( sendRequests_.size() ),
                &sendRequests_[0],
                MPI_STATUSES_IGNORE );

   sendRequests_.clear();
}



void KnownSizeCommunication::scheduleReceives( std::map<MPIRank, ReceiveInfo> & recvInfos )
{
   WALBERLA_NON_MPI_SECTION() { WALBERLA_ASSERT( false ); }

   WALBERLA_ASSERT( ! receiving_ );

   recvRequests_.assign( recvInfos.size(), MPI_REQUEST_NULL );
   size_t recvCount = 0;

   for( auto it = recvInfos.begin(); it != recvInfos.end(); ++it )
   {
      const MPIRank senderRank = it->first;
      ReceiveInfo & recvInfo   = it->second;

      // This is the known-size communication -> here valid sizes are needed
      WALBERLA_ASSERT_GREATER( recvInfo.size, 0 );

      recvInfo.buffer.resize( uint_c( recvInfo.size ) );

      // Schedule receive for content
      MPI_Irecv( recvInfo.buffer.ptr(),     // pointer to size field of recvInfo
                 recvInfo.size,             // size of expected message
                 MPI_BYTE,                  // type
                 senderRank,                // rank of sender process
                 tag_,                      // message tag
                 communicator_,             // communicator
                 & recvRequests_[recvCount] // request, needed for wait
                 );

      ++recvCount;
   }

   WALBERLA_ASSERT_EQUAL( recvCount, recvRequests_.size() );

   receiving_ = true;
}


MPIRank KnownSizeCommunication::waitForNextReceive( std::map<MPIRank, ReceiveInfo> & recvInfos )
{
   WALBERLA_NON_MPI_SECTION() { WALBERLA_ASSERT( false ); }

   WALBERLA_ASSERT( receiving_ );

   if( recvRequests_.empty() ) {
      receiving_ = false;
      return INVALID_RANK;
   }

   MPI_Status status;
   int requestIndex = -1; // output parameter initialized with invalid value
   MPI_Waitany( int_c( recvRequests_.size() ),
                & recvRequests_[0],
                & requestIndex,
                & status );

   if ( requestIndex == MPI_UNDEFINED )
   {
      recvRequests_.clear();
      receiving_ = false;
      return INVALID_RANK;
   }


   WALBERLA_ASSERT_GREATER_EQUAL( requestIndex, 0 );
   WALBERLA_ASSERT_LESS( requestIndex, int_c( recvRequests_.size() ) );

   recvRequests_[ uint_c( requestIndex ) ] = MPI_REQUEST_NULL;

   MPIRank senderRank = status.MPI_SOURCE;
   WALBERLA_ASSERT_GREATER_EQUAL( senderRank, 0 );


#ifndef NDEBUG
   int receivedBytes;
   MPI_Get_count( &status, MPI_BYTE, &receivedBytes );
   WALBERLA_ASSERT_EQUAL ( recvInfos[senderRank].size, receivedBytes );
#else
   WALBERLA_UNUSED( recvInfos );
#endif

   return senderRank;
}




//======================================================================================================================
//
//  Unknown Size Communication
//
//======================================================================================================================


void UnknownSizeCommunication::send( MPIRank receiver, const SendBuffer & sendBuffer )
{
   WALBERLA_NON_MPI_SECTION() { WALBERLA_ASSERT( false ); }

   if ( ! sending_ )
      sending_ = true;


   // Send size message
   outgoingBufferForSizes_.push_back( int_c( sendBuffer.size() ) );
   MPISize & sizeBuffer = outgoingBufferForSizes_.back();

   sendRequests_.push_back( MPI_REQUEST_NULL );
   MPI_Request & sizeMsgReq = sendRequests_.back();

   MPI_Isend( &sizeBuffer ,             // pointer to size buffer
             1,                         // one integer is sent
             MPITrait<MPISize>::type(), // type
             receiver,                  // receiver rank
             tag_,                      // message tag
             communicator_,             // communicator
             & sizeMsgReq               // request needed for wait
             );


   // Send content message

   sendRequests_.push_back( MPI_REQUEST_NULL );
   MPI_Request & contentMsgReq = sendRequests_.back();

   MPI_Isend(sendBuffer.ptr(),           // pointer to size buffer
             int_c( sendBuffer.size() ), // send one size
             MPI_BYTE,                   // type
             receiver,                   // receiver rank
             tag_,                       // message tag
             communicator_,              // communicator
             & contentMsgReq             // request needed for wait
             );
}

void UnknownSizeCommunication::waitForSends()
{
   WALBERLA_NON_MPI_SECTION() { WALBERLA_ASSERT( false ); }

   sending_ = false;

   if ( sendRequests_.empty() )
      return;

   MPI_Waitall( int_c( sendRequests_.size() ),
                &sendRequests_[0],
                MPI_STATUSES_IGNORE );

   sendRequests_.clear();
   outgoingBufferForSizes_.clear();
}

void UnknownSizeCommunication::scheduleReceives( std::map<MPIRank, ReceiveInfo> & recvInfos )
{
   WALBERLA_NON_MPI_SECTION() { WALBERLA_ASSERT( false ); }

   receiving_ = true;

   recvRequests_.assign ( recvInfos.size(), MPI_REQUEST_NULL );
   sizeAlreadyReceived_.assign( recvInfos.size(), false );

   // Array where communicated message size is stored

   size_t recvCount = 0;
   for( auto it = recvInfos.begin(); it != recvInfos.end(); ++it )
   {
      const MPIRank sender   = it->first;
      ReceiveInfo & recvInfo = it->second;
      recvInfo.size = INVALID_SIZE;
      MPISize & targetLocation = recvInfo.size;
      MPI_Irecv( &targetLocation,            // where to store received size
                 1,                          // size of expected message
                 MPITrait<MPISize>::type(),  // type
                 sender,                     // rank of sender process
                 tag_,                       // message tag
                 communicator_,              // communicator
                 & recvRequests_[recvCount]  // request, needed for wait
                 );

      ++recvCount;
   }
   WALBERLA_ASSERT_EQUAL( recvCount, recvRequests_.size() );
}


MPIRank UnknownSizeCommunication::waitForNextReceive( std::map<MPIRank, ReceiveInfo> & recvInfos )
{
   WALBERLA_NON_MPI_SECTION() { WALBERLA_ASSERT( false ); }

   WALBERLA_ASSERT( receiving_ );

   if( recvRequests_.empty() ) {
      receiving_ = false;
      return INVALID_RANK;
   }

   // On first entry in this function:
   //  - receive request vector contains requests for size messages
   //  - all ReceiveInfo's have sizeReceived=false
   while( true ) // this loops as long as size messages are received
   {
      MPI_Status status;
      int requestIndex = -1; // output parameter initialized with invalid value

      MPI_Waitany( int_c( recvRequests_.size() ),
                   & recvRequests_[0],
                   & requestIndex,
                   & status );

      if ( requestIndex == MPI_UNDEFINED )
      {
         recvRequests_.clear();
         sizeAlreadyReceived_.clear();
         receiving_ = false;
         return INVALID_RANK;
      }

      WALBERLA_ASSERT_GREATER_EQUAL( requestIndex, 0 );
      WALBERLA_ASSERT_LESS( requestIndex, int_c( recvRequests_.size() ) );

      recvRequests_[ uint_c( requestIndex ) ] = MPI_REQUEST_NULL;

      MPIRank senderRank = status.MPI_SOURCE;
      WALBERLA_ASSERT_GREATER_EQUAL( senderRank, 0 );

      int receivedBytes;
      MPI_Get_count( &status, MPI_BYTE, &receivedBytes );


      ReceiveInfo & recvInfo = recvInfos[ senderRank ];

      if ( ! sizeAlreadyReceived_[ uint_c( requestIndex ) ] )
      {
         // a size message was received

         WALBERLA_ASSERT_GREATER( recvInfo.size, 0 );
         WALBERLA_ASSERT_EQUAL( receivedBytes, sizeof(MPISize) );

         recvInfo.buffer.resize( uint_c( recvInfo.size ) );
         sizeAlreadyReceived_[ uint_c( requestIndex ) ] = true;

         // Schedule receive for content
         MPI_Irecv( recvInfo.buffer.ptr(),        // pointer to size field of recvInfo
                    recvInfo.size,                // size of expected message
                    MPI_BYTE,                     // type
                    senderRank,                   // rank of sender process
                    tag_,                         // message tag
                    communicator_,                // communicator
                    & recvRequests_[ uint_c( requestIndex ) ] // request, needed for wait
                    );

      }
      else // received content message
      {
         WALBERLA_ASSERT_GREATER( recvInfo.size , 0 );
         WALBERLA_ASSERT_EQUAL( receivedBytes, recvInfo.size );
         WALBERLA_ASSERT( sizeAlreadyReceived_[ uint_c(requestIndex) ] );

         return senderRank;
      }
   }

   WALBERLA_ASSERT( false );
   return INVALID_RANK; //cannot happen - only to prevent compiler warnings
}




//======================================================================================================================
//
//  NoMPI Communication
//
//======================================================================================================================


void NoMPICommunication::send( MPIRank receiver, const SendBuffer & sendBuffer )
{
   WALBERLA_UNUSED( receiver);

   WALBERLA_ASSERT_EQUAL( receiver, 0 );
   tmpBuffer_ = sendBuffer;
   received_ = false;
}

void NoMPICommunication::waitForSends()
{
   return;
}

void NoMPICommunication::scheduleReceives( std::map<MPIRank, ReceiveInfo> & recvInfos )
{
   WALBERLA_DEBUG_SECTION()
   {
      switch( recvInfos.size() )
      {
      case size_t(0):  break;
      case size_t(1):  WALBERLA_ASSERT_EQUAL( recvInfos.begin()->first, 0 ); break;
      default:         WALBERLA_ABORT("BufferSystem may only communicate with rank 0 in NonMPI mode!");
      }
   }
}


MPIRank NoMPICommunication::waitForNextReceive( std::map<MPIRank, ReceiveInfo> & recvInfos )
{
   if( recvInfos.empty() )
   {
      received_ = true;
      return INVALID_RANK;
   }

   WALBERLA_ASSERT_EQUAL( recvInfos.size(), size_t( 1 ) );
   WALBERLA_ASSERT_EQUAL( recvInfos.begin()->first, 0 );

   if( received_ )
   {
      return INVALID_RANK;
   }

   recvInfos[0].buffer = tmpBuffer_;
   received_ = true;
   return 0;
}





} // namespace internal
} // namespace mpi
} // namespace walberla
