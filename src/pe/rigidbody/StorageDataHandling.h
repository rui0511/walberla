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
//! \file BodyStorageDataHandling.h
//! \author Sebastian Eibl <sebastian.eibl@fau.de>
//
//======================================================================================================================

#pragma once

#include "BodyIterators.h"
#include "BodyStorage.h"
#include "pe/Types.h"

#include "pe/communication/PackNotification.h"
#include "pe/communication/RigidBodyCopyNotification.h"
#include "pe/communication/DynamicMarshalling.h"

#include "blockforest/BlockDataHandling.h"
#include "domain_decomposition/BlockStorage.h"
#include "core/Abort.h"

namespace walberla{
namespace pe{


template<typename BodyTuple>
class StorageDataHandling : public blockforest::BlockDataHandling<Storage>{
public:
   virtual ~StorageDataHandling() {}

   /// must be thread-safe !
   virtual Storage * initialize( IBlock * const block ) WALBERLA_OVERRIDE;

   /// must be thread-safe !
   virtual void serialize( IBlock * const block, const BlockDataID & id, mpi::SendBuffer & buffer ) WALBERLA_OVERRIDE;
   /// must be thread-safe !
   virtual Storage * deserialize( IBlock * const block ) WALBERLA_OVERRIDE;
   /// must be thread-safe !
   virtual void deserialize( IBlock * const block, const BlockDataID & id, mpi::RecvBuffer & buffer ) WALBERLA_OVERRIDE;

   /// must be thread-safe !
   virtual void serializeCoarseToFine( Block * const block, const BlockDataID & id, mpi::SendBuffer & buffer, const uint_t child ) WALBERLA_OVERRIDE;
   /// must be thread-safe !
   virtual void serializeFineToCoarse( Block * const block, const BlockDataID & id, mpi::SendBuffer & buffer ) WALBERLA_OVERRIDE;

   /// must be thread-safe !
   virtual Storage * deserializeCoarseToFine( Block * const block ) WALBERLA_OVERRIDE;
   /// must be thread-safe !
   virtual Storage * deserializeFineToCoarse( Block * const block ) WALBERLA_OVERRIDE;

   /// must be thread-safe !
   virtual void deserializeCoarseToFine( Block * const block, const BlockDataID & id, mpi::RecvBuffer & buffer ) WALBERLA_OVERRIDE;
   /// must be thread-safe !
   virtual void deserializeFineToCoarse( Block * const block, const BlockDataID & id, mpi::RecvBuffer & buffer, const uint_t child ) WALBERLA_OVERRIDE;

private:
   void deserializeImpl( IBlock * const block, const BlockDataID & id, mpi::RecvBuffer & buffer );
};

template<typename BodyTuple>
Storage * StorageDataHandling<BodyTuple>::initialize( IBlock * const /*block*/ )
{
   return new Storage();
}

template<typename BodyTuple>
void StorageDataHandling<BodyTuple>::serialize( IBlock * const block, const BlockDataID & id, mpi::SendBuffer & buffer )
{
   using namespace walberla::pe::communication;
   BodyStorage& localBodyStorage = (*(block->getData< Storage >( id )))[0];
   buffer << localBodyStorage.size();
   for (auto bodyIt = localBodyStorage.begin(); bodyIt != localBodyStorage.end(); ++bodyIt)
   {
      if ( !block->getAABB().contains( bodyIt->getPosition()) )
      {
         WALBERLA_ABORT( "Body to be stored not contained within block!" );
      }
      marshal( buffer, RigidBodyCopyNotification( **bodyIt ) );
      MarshalDynamically<BodyTuple>::execute( buffer, **bodyIt );
   }
}

template<typename BodyTuple>
Storage * StorageDataHandling<BodyTuple>::deserialize( IBlock * const block )
{
   return initialize(block);
}

template<typename BodyTuple>
void StorageDataHandling<BodyTuple>::deserialize( IBlock * const block, const BlockDataID & id, mpi::RecvBuffer & buffer )
{
   WALBERLA_DEBUG_SECTION()
   {
      BodyStorage& localBodyStorage = (*(block->getData< Storage >( id )))[0];
      WALBERLA_CHECK_EQUAL( localBodyStorage.size(), 0);
   }
   deserializeImpl( block, id, buffer);
}

template<typename BodyTuple>
void StorageDataHandling<BodyTuple>::serializeCoarseToFine( Block * const block, const BlockDataID & id, mpi::SendBuffer & buffer, const uint_t child )
{
   // get child aabb
   const math::AABB aabb = block->getAABB();
   const real_t xMid = (aabb.xMax() + aabb.xMin()) * real_t(0.5);
   const real_t yMid = (aabb.yMax() + aabb.yMin()) * real_t(0.5);
   const real_t zMid = (aabb.zMax() + aabb.zMin()) * real_t(0.5);

   const real_t xMin = (child & uint_t(1)) ? xMid : aabb.xMin();
   const real_t xMax = (child & uint_t(1)) ? aabb.xMax() : xMid;

   const real_t yMin = (child & uint_t(2)) ? yMid : aabb.yMin();
   const real_t yMax = (child & uint_t(2)) ? aabb.yMax() : yMid;

   const real_t zMin = (child & uint_t(4)) ? zMid : aabb.zMin();
   const real_t zMax = (child & uint_t(4)) ? aabb.zMax() : zMid;

   const math::AABB childAABB(xMin, yMin, zMin, xMax, yMax, zMax);
   //WALBERLA_LOG_DEVEL( (child & uint_t(1)) << (child & uint_t(2)) << (child & uint_t(4)) << "\naabb: " << aabb << "\nchild: " << childAABB );

   using namespace walberla::pe::communication;

   BodyStorage& localBodyStorage = (*(block->getData< Storage >( id )))[0];

   decltype(localBodyStorage.size()) numOfParticles = 0;

   auto ptr = buffer.allocate<decltype(localBodyStorage.size())>();
   for (auto bodyIt = localBodyStorage.begin(); bodyIt != localBodyStorage.end(); ++bodyIt)
   {
      if ( !block->getAABB().contains( bodyIt->getPosition()) )
      {
         WALBERLA_ABORT( "Body to be stored not contained within block!" );
      }
      if( childAABB.contains( bodyIt->getPosition()) )
      {
         marshal( buffer, RigidBodyCopyNotification( **bodyIt ) );
         MarshalDynamically<BodyTuple>::execute( buffer, **bodyIt );
         ++numOfParticles;
      }
   }
   *ptr = numOfParticles;
}

template<typename BodyTuple>
void StorageDataHandling<BodyTuple>::serializeFineToCoarse( Block * const block, const BlockDataID & id, mpi::SendBuffer & buffer )
{
   using namespace walberla::pe::communication;
   BodyStorage& localBodyStorage = (*(block->getData< Storage >( id )))[0];
   buffer << localBodyStorage.size();
   for (auto bodyIt = localBodyStorage.begin(); bodyIt != localBodyStorage.end(); ++bodyIt)
   {
      if ( !block->getAABB().contains( bodyIt->getPosition()) )
      {
         WALBERLA_ABORT( "Body to be stored not contained within block!" );
      }
      marshal( buffer, RigidBodyCopyNotification( **bodyIt ) );
      MarshalDynamically<BodyTuple>::execute( buffer, **bodyIt );
   }
}

template<typename BodyTuple>
Storage * StorageDataHandling<BodyTuple>::deserializeCoarseToFine( Block * const block )
{
   return initialize(block);
}

template<typename BodyTuple>
Storage * StorageDataHandling<BodyTuple>::deserializeFineToCoarse( Block * const block )
{
   return initialize(block);
}

template<typename BodyTuple>
void StorageDataHandling<BodyTuple>::deserializeCoarseToFine( Block * const block, const BlockDataID & id, mpi::RecvBuffer & buffer )
{
   WALBERLA_DEBUG_SECTION()
   {
      BodyStorage& localBodyStorage = (*(block->getData< Storage >( id )))[0];
      WALBERLA_CHECK_EQUAL( localBodyStorage.size(), 0);
   }
   deserializeImpl( block, id, buffer);
}

template<typename BodyTuple>
void StorageDataHandling<BodyTuple>::deserializeFineToCoarse( Block * const block, const BlockDataID & id, mpi::RecvBuffer & buffer, const uint_t /*child*/ )
{
   deserializeImpl( block, id, buffer);
}

template<typename BodyTuple>
void StorageDataHandling<BodyTuple>::deserializeImpl( IBlock * const block, const BlockDataID & id, mpi::RecvBuffer & buffer )
{
   using namespace walberla::pe::communication;

   BodyStorage& localBodyStorage = (*(block->getData< Storage >( id )))[0];
   decltype(localBodyStorage.size()) numBodies = 0;
   buffer >> numBodies;

   while( numBodies > 0 )
   {
      typename RigidBodyCopyNotification::Parameters objparam;
      unmarshal( buffer, objparam );

      BodyID bd = UnmarshalDynamically<BodyTuple>::execute(buffer, objparam.geomType_, block->getBlockStorage().getDomain(), block->getAABB());
      bd->setRemote( false );
      bd->MPITrait.setOwner(Owner(MPIManager::instance()->rank(), block->getId().getID()));

      if ( !block->getAABB().contains( bd->getPosition()) )
      {
         WALBERLA_ABORT("Loaded body not contained within block!\n" << "aabb: " << block->getAABB() << "\nparticle:" << bd );
      }
      WALBERLA_ASSERT_EQUAL(localBodyStorage.find( bd->getSystemID() ), localBodyStorage.end());
      localBodyStorage.add(bd);

      --numBodies;
   }
}

template <typename BodyTuple>
shared_ptr<StorageDataHandling<BodyTuple> > createStorageDataHandling()
{
   return make_shared<StorageDataHandling<BodyTuple> >( );
}

}
}
