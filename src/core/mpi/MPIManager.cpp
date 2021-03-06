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
//! \file MPIManager.cpp
//! \ingroup core
//! \author Martin Bauer <martin.bauer@fau.de>
//! \author Florian Schornbaum <florian.schornbaum@fau.de>
//! \author Christian Godenschwager <christian.godenschwager@fau.de>
//
//======================================================================================================================

#include "MPIManager.h"

#include <boost/exception_ptr.hpp>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>


namespace walberla{
namespace mpi {


/**
 * Terminate Handler that calls MPI_Abort instead of std::abort
 *
 * Terminate handler is called when an exception is not caught
 * and the program has to be aborted
 * The standard terminate handler prints exception.what() and
 * calls std::abort(). We overwrite the terminate handler when MPI was
 * initialized, to call MPI_Abort in this case.
 */
static void customTerminateHandler()
{
   std::cerr << "Execution failed: Uncaught Exception." << std::endl;

   // The standard terminate handler prints the exception text,
   // Here we want to do the same. To get the exception we use a hack
   // that only works on linux environments: We retrieve the current_exception() pointer
   // which strictly is only allowed in catch() blocks, however works here as well, if we are
   // in linux environments. If this pointer is null,
   // i.e. in cases when this hack does not work, we just cannot print the message
   // otherwise we re-throw the exception to get the type, and print exception.what()
   try {
      if(boost::current_exception() )
         boost::rethrow_exception(boost::current_exception());
   } catch (std::exception const & exc) {
      std::cerr << exc.what() << std::endl;
   }

   WALBERLA_MPI_SECTION() {
      MPIManager::instance()->abort();
   }
   else {
      std::abort();
   }
}



MPIManager::~MPIManager()
{
   finalizeMPI();
}

void MPIManager::abort()
{
   currentlyAborting_ = true;
   WALBERLA_MPI_SECTION()
   {
      if( MPIManager::instance()->isMPIInitialized() )
         MPI_Abort( MPI_COMM_WORLD, EXIT_FAILURE );
   }
   std::exit( EXIT_FAILURE );
}


void MPIManager::initializeMPI( int* argc, char*** argv, bool abortOnException )
{
   WALBERLA_MPI_SECTION()
   {
      WALBERLA_ASSERT( !isMPIInitialized_ );

      // Check first that MPI was not initialized before
      // f.e. when using Python, MPI could have been initialized by
      // a different MPI module like mpi4py
      int mpiAlreadyInitialized=0;
      MPI_Initialized( &mpiAlreadyInitialized );
      if ( ! mpiAlreadyInitialized ) {
         MPI_Init( argc, argv );
      }

      isMPIInitialized_ = true;
      MPI_Comm_size( MPI_COMM_WORLD, &numProcesses_ );
      MPI_Comm_rank( MPI_COMM_WORLD, &worldRank_ );

      if( abortOnException )
         std::set_terminate( customTerminateHandler );
   }
}



void MPIManager::finalizeMPI()
{
   WALBERLA_MPI_SECTION() {
      if( isMPIInitialized_ && !currentlyAborting_ )
      {
         isMPIInitialized_ = false;
         MPI_Finalize();
      }
   }
}



void MPIManager::resetMPI()
{
   WALBERLA_MPI_SECTION() {
      WALBERLA_ASSERT( isMPIInitialized_ );
      if( rank_ != -1 )
      {
         if( comm_ == MPI_COMM_WORLD )
            comm_ = MPI_COMM_NULL;
         else
            MPI_Comm_free( &comm_ );
         rank_ = -1;
      }
      cartesianSetup_ = false;
      WALBERLA_ASSERT_EQUAL( comm_, MPI_COMM_NULL );
      WALBERLA_ASSERT_EQUAL( rank_, -1 );
   }
}



void MPIManager::createCartesianComm( int dims[3], int periodicity[3] )
{
   WALBERLA_ASSERT( isMPIInitialized_ );
   WALBERLA_ASSERT_EQUAL( rank_, -1 );
   WALBERLA_ASSERT( !cartesianSetup_ );
   WALBERLA_ASSERT_GREATER( dims[0], 0 );
   WALBERLA_ASSERT_GREATER( dims[1], 0 );
   WALBERLA_ASSERT_GREATER( dims[2], 0 );

   MPI_Cart_create( MPI_COMM_WORLD, 3, dims, periodicity, true, &comm_ );

   WALBERLA_ASSERT_UNEQUAL(comm_, MPI_COMM_NULL);

   MPI_Comm_rank( comm_, &rank_ );
   cartesianSetup_ = true;
}



void MPIManager::createCartesianComm( const uint_t xProcesses, const uint_t  yProcesses, const uint_t zProcesses,
                                      const bool   xPeriodic,  const bool    yPeriodic,  const bool   zPeriodic )
{
   int dims[3];
   dims[0] = numeric_cast<int>( xProcesses );
   dims[1] = numeric_cast<int>( yProcesses );
   dims[2] = numeric_cast<int>( zProcesses );

   int periodicity[3];
   periodicity[0] = xPeriodic ? 1 : 0;
   periodicity[1] = yPeriodic ? 1 : 0;
   periodicity[2] = zPeriodic ? 1 : 0;

   createCartesianComm( dims, periodicity );
}



void MPIManager::cartesianCoord( int coordOut[3] ) const
{
   cartesianCoord( rank_, coordOut );
}



void MPIManager::cartesianCoord( int rankIn, int coordOut[3] ) const
{
   WALBERLA_ASSERT( isMPIInitialized_ );
   WALBERLA_ASSERT( cartesianSetup_ );
   WALBERLA_ASSERT_UNEQUAL( comm_, MPI_COMM_NULL );

   MPI_Cart_coords( comm_, rankIn, 3, coordOut );
}



int MPIManager::cartesianRank( int coords[3] ) const
{
   WALBERLA_ASSERT( isMPIInitialized_ );
   WALBERLA_ASSERT( cartesianSetup_ );
   WALBERLA_ASSERT_UNEQUAL( comm_, MPI_COMM_NULL );

   int r;
   MPI_Cart_rank( comm_, coords, &r );
   return r;
}



int MPIManager::cartesianRank( const uint_t x, const uint_t y, const uint_t z ) const
{
   int coords[3];
   coords[0] = numeric_cast<int>(x);
   coords[1] = numeric_cast<int>(y);
   coords[2] = numeric_cast<int>(z);

   return cartesianRank( coords );
}

std::string MPIManager::getMPIErrorString(int errorCode)
{
   WALBERLA_NON_MPI_SECTION()
   {
      throw std::logic_error("Trying to use function 'MPIManager::getMPIErrorString' but waLBerla is compiled without MPI-support!");
   }
   WALBERLA_ASSERT_GREATER(MPI_MAX_ERROR_STRING, 0);
   std::vector<char> errorString(MPI_MAX_ERROR_STRING);
   int resultLen;

   MPI_Error_string(errorCode, &errorString[0], &resultLen);

   WALBERLA_ASSERT_GREATER_EQUAL(resultLen, 0);
   WALBERLA_ASSERT_LESS_EQUAL(resultLen, numeric_cast<int>(errorString.size()));
   return std::string(errorString.begin(), errorString.begin() + resultLen);
}

std::string MPIManager::getMPICommName(MPI_Comm comm)
{
   WALBERLA_NON_MPI_SECTION()
   {
      throw std::logic_error("Trying to use function 'MPIManager::getMPICommName' but waLBerla is compiled without MPI-support!");
   }
   WALBERLA_ASSERT_GREATER(MPI_MAX_OBJECT_NAME, 0);
   std::vector<char> commName(MPI_MAX_OBJECT_NAME);
   int resultLen;

   MPI_Comm_get_name(comm, &commName[0], &resultLen);

   WALBERLA_ASSERT_GREATER_EQUAL(resultLen, 0);
   WALBERLA_ASSERT_LESS_EQUAL(resultLen, numeric_cast<int>(commName.size()));
   return std::string(commName.begin(), commName.begin() + resultLen);
}

} // namespace mpi
} // namespace walberla
