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
//! \file Kernel.h
//! \ingroup cuda
//! \author Martin Bauer <martin.bauer@fau.de>
//
//======================================================================================================================

#pragma once

#include "core/Abort.h"
#include "core/debug/Debug.h"

#include "ErrorChecking.h"

#include <cuda_runtime.h>
#include <boost/type_traits.hpp>
#include <vector>



namespace walberla {
namespace cuda {


   //*******************************************************************************************************************
   /*! Wrapper class around a CUDA kernel, to call kernels also from code not compiled with nvcc
   *
   * Example:
   * \code
         // Declaration of kernel, implementation has to be in a file compiled with nvcc
         void kernel_func ( double * inputData, int size );

         auto kernel = make_kernel( kernel_func );
         kernel.addParam<double*> ( argument1 );
         kernel.addParam<int>     ( 20 );
         kernel.configure( dim3( 3,3,3), dim3( 4,4,4) );
         kernel();
         // this code is equivalent to:
         kernel_func<<< dim3( 3,3,3), dim3( 4,4,4) >> ( argument1, 20 );
   * \endcode
   *
   * Why use this strange wrapper class instead of the nice kernel call syntax "<<<griddim, blockdim >>>" ??
   *     - This syntax is nice but has to be compiled with nvcc, which does not (yet) understand C++11
   *     - C++11 features are used all over the place in waLBerla code
   *     - all *.cu files and headers included in *.cu files have to be "C++11 free"
   *     - thus there should be as few code as possible in *.cu files
   *
   * Drawbacks of this class compared to kernel call syntax:
   * Type checking of parameters can only be done at runtime (is done only in Debug mode!).
   * Consider the following example:
   * \code
         // Declaration of kernel, implementation has to be in a file compiled with nvcc
         void kernel_func ( double * inputData, int size );

         auto kernel = make_kernel( kernel_func );
         kernel.addParam<float*>       ( argument1 );
         kernel.addParam<unsigned int> ( 40 );
         kernel.configure( dim3( 3,3,3), dim3( 4,4,4) );
         kernel();
         // this code is equivalent to:
         kernel_func<<< dim3( 3,3,3), dim3( 4,4,4) >> ( argument1, 20 );
   * \endcode
   * The parameter types of the kernel and the parameters added at the cuda::Kernel class do not match.
   * This is only detected when the code is run and was compiled in DEBUG mode!
   *
   *
   * Advantages of this class compared to kernel call syntax: Integrates nicely with waLBerlas field indexing and
   * accessor concepts:
   * \code
         void kernel_func( cuda::SimpleFieldAccessor<double> f );

         auto myKernel = cuda::make_kernel( &kernel_double );
         myKernel.addFieldIndexingParam( cuda::SimpleFieldIndexing<double>::xyz( gpuField ) );
         myKernel();
   * \endcode
   * When using at least one FieldIndexingParameter configure() does not have to be called, since the thread and grid
   * setup is done by the indexing scheme. If two FieldIndexingParameters are passed, the two indexing schemes have to
   * be consistent.
   */
   //*******************************************************************************************************************
   template<typename FuncPtr>
   class Kernel
   {
   public:
      Kernel( FuncPtr funcPtr );

      template<typename T>  void addParam( const T & param );
      template<typename T>  void addFieldIndexingParam( const T & indexing );


      void configure( dim3 gridDim, dim3 blockDim, std::size_t sharedMemSize = 0 );
      void operator() ( cudaStream_t stream = 0 ) const;


   protected:
      template<typename T> size_t determineNextOffset();


      //** Members        **********************************************************************************************
      /*! \name Members  */
      //@{
      FuncPtr funcPtr_;

      bool configured_;
      dim3 gridDim_;
      dim3 blockDim_;
      std::size_t sharedMemSize_;

      struct ParamInfo {
         std::vector<char> data;
         size_t offset;
      };
      std::vector< ParamInfo > params_;
      //@}
      //****************************************************************************************************************


      //** Type checking of parameters **********************************************************************************
      /*! \name Type checking of parameters  */
      //@{
      typedef typename boost::remove_pointer<FuncPtr>::type FuncType;

      #define CHECK_PARAMETER_FUNC( Number ) \
      template<typename T> \
      bool checkParameter##Number( typename boost::enable_if_c< (boost::function_traits<FuncType>::arity >= Number ), T >::type *  = 0 ) { \
         return boost::is_same< T, typename boost::function_traits<FuncType>::arg##Number##_type >::value; \
      } \
      template<typename T> \
      bool checkParameter##Number( typename boost::disable_if_c< (boost::function_traits<FuncType>::arity >= Number ),T >::type *  = 0 ) { \
         return false; \
      }

      CHECK_PARAMETER_FUNC(1)
      CHECK_PARAMETER_FUNC(2)
      CHECK_PARAMETER_FUNC(3)
      CHECK_PARAMETER_FUNC(4)
      CHECK_PARAMETER_FUNC(5)
      CHECK_PARAMETER_FUNC(6)
      CHECK_PARAMETER_FUNC(7)
      CHECK_PARAMETER_FUNC(8)

      template<typename T> bool checkParameter( uint_t n );
      //@}
      //****************************************************************************************************************
   };


   template<typename FuncPtr>
   Kernel<FuncPtr> make_kernel( FuncPtr funcPtr ) {
      return Kernel<FuncPtr> ( funcPtr );
   }







   //===================================================================================================================
   //
   //  Implementation
   //
   //===================================================================================================================

   template<typename FP>
   Kernel<FP>::Kernel( FP funcPtr )
      : funcPtr_ ( funcPtr ),
        configured_( false ),
        sharedMemSize_( 0 )
   {}

   template<typename FP>
   template<typename T>
   void Kernel<FP>::addParam( const T & param )
   {
      ParamInfo paramInfo;
      paramInfo.data.resize( sizeof(T) );
      std::memcpy ( &(paramInfo.data[0]), &param, sizeof(T) );
      paramInfo.offset = determineNextOffset<T>();

      WALBERLA_ASSERT( checkParameter<T>( params_.size() +1 ),
                       "cuda::Kernel type mismatch of parameter " << params_.size() +1  );

      params_.push_back( paramInfo );
   }


   template<typename FP>
   template<typename Indexing>
   void Kernel<FP>::addFieldIndexingParam( const Indexing & indexing )
   {
      configure( indexing.gridDim(), indexing.blockDim() );
      addParam( indexing.gpuAccess() );
   }

   template<typename FP>
   void Kernel<FP>::configure( dim3 gridDim, dim3 blockDim, std::size_t sharedMemSize )
   {
      if ( ! configured_ )
      {
         gridDim_ = gridDim;
         blockDim_ = blockDim;
         sharedMemSize_ = sharedMemSize;
         configured_ = true;
      }
      else
      {
         if ( gridDim.x  != gridDim_.x  || gridDim.y != gridDim_.y   || gridDim.z != gridDim_.z ||
              blockDim.x != blockDim_.x || blockDim.y != blockDim_.y || blockDim.z != blockDim_.z  )
         {
            WALBERLA_ABORT( "Error when configuring cuda::Kernel: Inconsistent setup. " );
         }
      }
   }

   template<typename FP>
   void Kernel<FP>::operator() ( cudaStream_t stream ) const
   {
      // check for correct number of parameter calls

      if ( params_.size() != boost::function_traits<FuncType>::arity ) {
         WALBERLA_ABORT( "Error when calling cuda::Kernel - Wrong number of arguments. " <<
                         "Expected " << boost::function_traits<FuncType>::arity << ", received " << params_.size() );
      }

      // set the number of blocks and  threads,
      WALBERLA_CUDA_CHECK( cudaConfigureCall( gridDim_, blockDim_, sharedMemSize_, stream ) );

      // register all parameters
      for( auto paramIt = params_.begin(); paramIt != params_.end(); ++paramIt )  {
         const void * ptr = &(paramIt->data[0]);
         WALBERLA_CUDA_CHECK( cudaSetupArgument( ptr, paramIt->data.size(), paramIt->offset ) );
      }

      // .. and launch the kernel
      static_assert( sizeof(void *) == sizeof(void (*)(void)),
                     "object pointer and function pointer sizes must be equal" );
      // dirty casting trick to circumvent compiler warning
      // essentially the next two lines are:  cudaLaunch( funcPtr_ );
      void *q = (void*) &funcPtr_;
      WALBERLA_CUDA_CHECK( cudaLaunch( (const char*) ( *static_cast<void **>(q) )) );
   }


   template<typename FP>
   template<typename T>
   bool Kernel<FP>::checkParameter( uint_t n )
   {
      switch (n) {
         case 1: return checkParameter1<T>();
         case 2: return checkParameter2<T>();
         case 3: return checkParameter3<T>();
         case 4: return checkParameter4<T>();
         case 5: return checkParameter5<T>();
         case 6: return checkParameter6<T>();
         case 7: return checkParameter7<T>();
         case 8: return checkParameter8<T>();
         default:
            WALBERLA_ABORT("Too many parameters passed to kernel");
      }
      return false;
   }


   template<typename FP>
   template<typename T>
   size_t Kernel<FP>::determineNextOffset()
   {
      size_t currentOffset = 0;
      if ( !params_.empty() )
         currentOffset = params_.back().offset + params_.back().data.size();

      size_t alignment = __alignof( T );
      return (currentOffset + alignment-1) & ~(alignment-1);
   }




} // namespace cuda
} // namespace walberla


