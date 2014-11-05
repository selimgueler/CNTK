// Matrix.cpp -- main CPP file that contains all functions exported by the CNTKMath.dll
//
// <copyright file="Matrix.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//
#include "stdafx.h"
#include "basetypes.h"
#include "fileutil.h"
#include "Matrix.h"
#include <assert.h>
#include <math.h>
#include "GPUWatcher.h"     // bring in this class as well so that it gets exported from this DLL

#ifndef CPUONLY
#pragma comment (lib, "CNTKMathCUDA.lib")   // built by CNTKMathCUDA project
#endif

#pragma warning (disable: 4127) // conditional expression is constant; "if (sizeof(ElemType)==sizeof(float))" triggers this
#pragma warning (disable: 4239) // nonstandard extension; triggered by this pattern: "auto& second = transposeB ? b.m_GPUMatrix->Transpose() : *b.m_GPUMatrix;"
#pragma warning (disable: 4702) // unreachable code; triggered for unknown reasons

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif


//before calling the following macro the current matrix location and matrix type on MatrixPointerToCheck must have been set correctly
#define DISPATCH_MATRIX_ON_FLAG(MatrixPointerToCheck, MatrixPointerToSetFlag, CPUDense, GPUDense, CPUSparse, GPUSparse) \
    { \
        CurrentDataLocation curLocation = (MatrixPointerToCheck)->GetCurrentMatrixLocation(); \
        if (curLocation==CurrentDataLocation::GPU || curLocation==CurrentDataLocation::BOTH)   \
        { \
            if ((MatrixPointerToCheck)->GetMatrixType() != MatrixType::SPARSE) \
            { \
                GPUDense; \
                if (MatrixPointerToSetFlag != nullptr) \
                    ((Matrix*)MatrixPointerToSetFlag)->SetDataLocation(CurrentDataLocation::GPU, MatrixType::DENSE); \
            } \
            else \
            { \
                GPUSparse; \
                if (MatrixPointerToSetFlag != nullptr) \
                    ((Matrix*)MatrixPointerToSetFlag)->SetDataLocation(CurrentDataLocation::GPU, MatrixType::SPARSE); \
            } \
        } \
        else if (curLocation==CurrentDataLocation::CPU) \
        { \
            if ((MatrixPointerToCheck)->GetMatrixType() != MatrixType::SPARSE) \
            { \
                CPUDense; \
                if (MatrixPointerToSetFlag != nullptr) \
                    ((Matrix*)MatrixPointerToSetFlag)->SetDataLocation(CurrentDataLocation::CPU, MatrixType::DENSE); \
            } \
            else \
            { \
                CPUSparse; \
                if (MatrixPointerToSetFlag != nullptr) \
                    ((Matrix*)MatrixPointerToSetFlag)->SetDataLocation(CurrentDataLocation::CPU, MatrixType::SPARSE); \
            } \
        } \
        else \
        { \
            throw std::runtime_error("Matrices do not exist in either CPU or GPU."); \
        } \
    }

//before calling the following macro the current matrix location and matrix type on MatrixPointerToCheck must have been set correctly
#define DISPATCH_MATRIX_ON_FLAG_USECPU_4BOTH(MatrixPointerToCheck, MatrixPointerToSetFlag, CPUDense, GPUDense, CPUSparse, GPUSparse) \
    { \
        CurrentDataLocation curLocation = (MatrixPointerToCheck)->GetCurrentMatrixLocation(); \
        if (curLocation==CurrentDataLocation::GPU)   \
        { \
            if ((MatrixPointerToCheck)->GetMatrixType() != MatrixType::SPARSE) \
            { \
                GPUDense; \
                if (MatrixPointerToSetFlag != nullptr) \
                    ((Matrix*)MatrixPointerToSetFlag)->SetDataLocation(CurrentDataLocation::GPU, MatrixType::DENSE); \
            } \
            else \
            { \
                GPUSparse; \
                if (MatrixPointerToSetFlag != nullptr) \
                    ((Matrix*)MatrixPointerToSetFlag)->SetDataLocation(CurrentDataLocation::GPU, MatrixType::SPARSE); \
            } \
        } \
        else if (curLocation==CurrentDataLocation::CPU || curLocation==CurrentDataLocation::BOTH) \
        { \
            if ((MatrixPointerToCheck)->GetMatrixType() != MatrixType::SPARSE) \
            { \
                CPUDense; \
                if (MatrixPointerToSetFlag != nullptr) \
                    ((Matrix*)MatrixPointerToSetFlag)->SetDataLocation(CurrentDataLocation::CPU, MatrixType::DENSE); \
            } \
            else \
            { \
                CPUSparse; \
                if (MatrixPointerToSetFlag != nullptr) \
                    ((Matrix*)MatrixPointerToSetFlag)->SetDataLocation(CurrentDataLocation::CPU, MatrixType::SPARSE); \
            } \
        } \
        else \
        { \
            throw std::runtime_error("Matrices do not exist in either CPU or GPU."); \
        } \
    }

namespace Microsoft { namespace MSR { namespace CNTK {
#pragma region Constructors, destructors and other static matrix builders


    //This function will only initialize default bland matrix. The actual matrices need to allocated
    //after calling this function and flags need to set correctly by calling SetDataLocation.
    template<class ElemType>
    void Matrix<ElemType>::Init(short deviceId)
    {
        m_baseMatrix=NULL;
        m_GPUMatrix=NULL;
        m_CPUMatrix=NULL;
        m_GPUSparseMatrix=NULL;
        m_CPUSparseMatrix=NULL;
        m_currentDataLocation = CurrentDataLocation::NONE;   
        m_matrixType = MatrixType::UNDETERMINED;

        int _devId = deviceId!=AUTOPLACEMATRIX ? deviceId : GetBestGPUDeviceId();
        m_preferredDeviceId=_devId;
        }

    //this function is used to indicate where (CPUDense, CPUSparse, GPUDense, GPUSparse) the most updated results are in
    //after the actual matrix is updated.
    template<class ElemType>
    void Matrix<ElemType>::SetDataLocation(CurrentDataLocation location, MatrixType type) const
    {
        m_currentDataLocation = location;

        // set the matrix type if passed in
        if (type != MatrixType::UNDETERMINED)
        {
            m_matrixType = type;
        }

        if (m_matrixType == MatrixType::DENSE)
        {        
            m_baseMatrix = ((m_currentDataLocation == CurrentDataLocation::CPU)?(BaseMatrix<ElemType>*)m_CPUMatrix:(BaseMatrix<ElemType>*)m_GPUMatrix);
        }
        else if (m_matrixType == MatrixType::SPARSE)
        {
            m_baseMatrix = ((m_currentDataLocation == CurrentDataLocation::CPU)?(BaseMatrix<ElemType>*)m_CPUSparseMatrix:(BaseMatrix<ElemType>*)m_GPUSparseMatrix);
        }
    }

    //this is a private constructor only used internally to initialize a blank matrix
    template<class ElemType>
    Matrix<ElemType>::Matrix(const MatrixFlags matrixFlags, const MatrixType matrixType, const MatrixFormat matrixFormat, short deviceID)
    {
        Init(deviceID);

        if (!(GetDeviceId() == MANAGEDEXTERN || (matrixFlags & matrixFlagDontOwnBuffer)))
            SwitchToMatrixType(matrixType, matrixFormat);
    }


    //this is a private constructor only used internally to initialize a blank matrix
    template<class ElemType>
    Matrix<ElemType>::Matrix(const MatrixFlags matrixFlags, const MatrixType matrixType, short deviceID)
    {    
        Init(deviceID);

        if (!(GetDeviceId() == MANAGEDEXTERN || (matrixFlags & matrixFlagDontOwnBuffer)))
            SwitchToMatrixType(matrixType, matrixType == MatrixType::DENSE? MatrixFormat::matrixFormatDense : MatrixFormat::matrixFormatSparseCSC);
    }

    //this is a private constructor only used internally to initialize a blank matrix
    template<class ElemType>
    Matrix<ElemType>::Matrix(const MatrixFlags matrixFlags, short deviceID)
    {
        Init(deviceID);

        if (!(GetDeviceId() == MANAGEDEXTERN || (matrixFlags & matrixFlagDontOwnBuffer)))
            SwitchToMatrixType(MatrixType::DENSE, MatrixFormat::matrixFormatDense);
    }

    template<class ElemType>
    Matrix<ElemType>::Matrix(short deviceID)
    {
        Init(deviceID);

        if (!(GetDeviceId() == MANAGEDEXTERN))
            SwitchToMatrixType(MatrixType::DENSE, MatrixFormat::matrixFormatDense);
    }

    // constructor for Matrix class to wrap an externally managed BaseMatrix
    // baseMatrix - base matrix for this element
    // pArray - pointer to current data array, will replace existing pointer in baseMatrix if != NULL
    // deviceId - deviceId where the pArray exists
    template<class ElemType>
    Matrix<ElemType>::Matrix(BaseMatrix<ElemType>* baseMatrix, ElemType *pArray, short deviceId) // constructor for setting Matrix from a base matrix
    {
        Init(deviceId);

        if (baseMatrix->GetFormat() & matrixFormatSparse)
        {
            if (m_preferredDeviceId == CPUDEVICE)
            {
                m_CPUSparseMatrix = (CPUSparseMatrix<ElemType>*)baseMatrix;
                SetDataLocation(CPU, SPARSE);
            }
            else
            {
                m_GPUSparseMatrix = (GPUSparseMatrix<ElemType>*)baseMatrix;
                SetDataLocation(GPU, SPARSE);
            }
        }
        else
        {
            if (m_preferredDeviceId == CPUDEVICE)
            {
                m_CPUMatrix = (CPUMatrix<ElemType>*)baseMatrix;
                SetDataLocation(CPU, DENSE);
            }
            else
            {
                m_GPUMatrix = (GPUMatrix<ElemType>*)baseMatrix;
                SetDataLocation(GPU, DENSE);
            }
        }
        m_baseMatrix = baseMatrix;
        m_baseMatrix->SetArray(pArray);
    }

    //matrixName is used to verify that correct matrix is read.
    template<class ElemType>
    Matrix<ElemType>::Matrix(FILE* f, const char * matrixName, short deviceId, const MatrixType matrixType)
    {
        if (deviceId == MANAGEDEXTERN)
            throw runtime_error("Externally Managed Matrix must use the basic constructor, then SetValue()\n");            

        Init(deviceId);

        if (matrixType == MatrixType::SPARSE)
        {
            if (m_preferredDeviceId == CPUDEVICE)
            {
                NOT_IMPLEMENTED;
                //m_CPUSparseMatrix = new CPUSparseMatrix<ElemType>(f,matrixName); 
                SetDataLocation(CPU, SPARSE);
            }
            else
            {
                NOT_IMPLEMENTED;
                //m_GPUSparseMatrix = new GPUSparseMatrix<ElemType>(f,matrixName, m_preferredDeviceId); 
                SetDataLocation(GPU, SPARSE);
            }
        }
        else
        {
            if (m_preferredDeviceId == CPUDEVICE)
            {
                m_CPUMatrix = new CPUMatrix<ElemType>(f,matrixName);
                SetDataLocation(CPU, DENSE);
            }
            else
            {
                m_GPUMatrix = new GPUMatrix<ElemType>(f,matrixName, m_preferredDeviceId);
                SetDataLocation(GPU, DENSE);
            }
        }
    }

    template<class ElemType>
    Matrix<ElemType>::Matrix(const size_t numRows, const size_t numCols, short deviceId, const MatrixType matrixType)
    {
        if (deviceId == MANAGEDEXTERN)
            throw runtime_error("Externally Managed Matrix must use the basic constructor, then SetValue(), or the full constructor\n");            

        Init(deviceId);

        if (matrixType == MatrixType::SPARSE)
        {
            if (m_preferredDeviceId == CPUDEVICE)
            {
                NOT_IMPLEMENTED;
                //m_CPUSparseMatrix = new CPUSparseMatrix<ElemType>(matrixFormatSparseCSC, numRows, numCols); 
                SetDataLocation(CPU, SPARSE);
            }
            else
            {
                NOT_IMPLEMENTED;
                //m_GPUSparseMatrix = new GPUSparseMatrix<ElemType>(matrixFormatSparseCSC, numRows, numCols, m_preferredDeviceId); 
                SetDataLocation(GPU, SPARSE);
            }
        }
        else
        {
            if (m_preferredDeviceId == CPUDEVICE)
            {
                m_CPUMatrix = new CPUMatrix<ElemType>(numRows, numCols);
                SetDataLocation(CPU, DENSE);
            }
            else
            {
                m_GPUMatrix = new GPUMatrix<ElemType>(numRows, numCols, m_preferredDeviceId);
                SetDataLocation(GPU, DENSE);
            }
        }
    }

    template<class ElemType>
    Matrix<ElemType>::Matrix(const size_t numRows, const size_t numCols, ElemType *pArray, const size_t matrixFlags, short deviceId, const size_t nnz)
    {
        Init(deviceId);

        if (m_preferredDeviceId == CPUDEVICE)
        {
            if (matrixFlags&matrixFormatSparse)
            {
                //WARNING: matrixFlag is not passed in and externally managed array cannot be passed in
                m_CPUSparseMatrix = new CPUSparseMatrix<ElemType>(matrixFormatSparseCSC, numRows,numCols, nnz);
                SetDataLocation(CPU, SPARSE);            
            }
            else
            {
                m_CPUMatrix = new CPUMatrix<ElemType>(numRows,numCols,pArray,matrixFlags);
                SetDataLocation(CPU, DENSE);            
            }
        }
        else
        {
            if (matrixFlags&matrixFormatSparse)
            {
                m_GPUSparseMatrix = new GPUSparseMatrix<ElemType>(numRows,numCols,nnz, pArray,matrixFlags,m_preferredDeviceId);
                SetDataLocation(GPU, SPARSE);            
            }
            else
            {
                m_GPUMatrix = new GPUMatrix<ElemType>(numRows,numCols,pArray,matrixFlags,m_preferredDeviceId);
                SetDataLocation(GPU, DENSE);            
            }
        }

        if (matrixFlagDontOwnBuffer & matrixFlags || m_preferredDeviceId == MANAGEDEXTERN)
            m_baseMatrix->SetOwnBuffer(false);
    }

    //copy constructor, deep copy
    template<class ElemType>
    Matrix<ElemType>::Matrix(const Matrix<ElemType>& deepCopyFrom, short deviceId)
    {
        if (deviceId == MANAGEDEXTERN)
            throw runtime_error("Externally Managed Matrix must use the basic constructor, then SetValue(), or the full constructor\n");            

        int origCopyFromDeviceId = deepCopyFrom.GetDeviceId();

        if (deviceId == AUTOPLACEMATRIX)  //use copyFrom's device if we have choice
            deviceId = (short)origCopyFromDeviceId;

        Init(deviceId);  //will set m_preferredDeviceId

        deepCopyFrom._transferToDevice(m_preferredDeviceId, true);

        DISPATCH_MATRIX_ON_FLAG(&deepCopyFrom,
            this,
            m_CPUMatrix = new CPUMatrix<ElemType>(*(deepCopyFrom.m_CPUMatrix)), 
            m_GPUMatrix = new GPUMatrix<ElemType>(*(deepCopyFrom.m_GPUMatrix)), 
            m_CPUSparseMatrix = new CPUSparseMatrix<ElemType>(*(deepCopyFrom.m_CPUSparseMatrix)), 
            NOT_IMPLEMENTED
            );

        //should we move back?
        deepCopyFrom._transferToDevice(origCopyFromDeviceId, true);

        m_preferredDeviceId = deepCopyFrom.m_preferredDeviceId;
            }

    //assignment operator, deep copy
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::operator=(const Matrix<ElemType>& deepCopyFrom)  
    {
        if (this != &deepCopyFrom)
        {
            SetValue(deepCopyFrom);
        }
        return *this;
    }

    //move constructor, shallow copy
    template<class ElemType>
    Matrix<ElemType>::Matrix(Matrix<ElemType>&& moveFrom)  
    {
        Init((short)moveFrom.GetDeviceId());

        DISPATCH_MATRIX_ON_FLAG(&moveFrom,
            this,
            m_CPUMatrix = new CPUMatrix<ElemType>(static_cast<CPUMatrix<ElemType>&&>(*(moveFrom.m_CPUMatrix))), 
            m_GPUMatrix = new GPUMatrix<ElemType>(static_cast<GPUMatrix<ElemType>&&>(*(moveFrom.m_GPUMatrix))), 
            NOT_IMPLEMENTED, 
            m_GPUSparseMatrix = new GPUSparseMatrix<ElemType>(static_cast<GPUSparseMatrix<ElemType>&&>(*(moveFrom.m_GPUSparseMatrix)))
            );

        m_preferredDeviceId = moveFrom.m_preferredDeviceId;
            }

    //move assignment operator, shallow copy
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::operator=(Matrix<ElemType>&& moveFrom)  
    {
        Clear();
        m_preferredDeviceId = moveFrom.m_preferredDeviceId;

        DISPATCH_MATRIX_ON_FLAG(&moveFrom,
            this,
            if (m_CPUMatrix != nullptr) m_CPUMatrix->operator=(static_cast<CPUMatrix<ElemType>&&>(*(moveFrom.m_CPUMatrix)));
            else m_CPUMatrix = new CPUMatrix<ElemType>(static_cast<CPUMatrix<ElemType>&&>(*(moveFrom.m_CPUMatrix))), 

            if (m_GPUMatrix != nullptr) m_GPUMatrix->operator=(static_cast<GPUMatrix<ElemType>&&>(*(moveFrom.m_GPUMatrix)));
            else m_GPUMatrix = new GPUMatrix<ElemType>(static_cast<GPUMatrix<ElemType>&&>(*(moveFrom.m_GPUMatrix))), 

            NOT_IMPLEMENTED, 

            if (m_GPUSparseMatrix != nullptr) m_GPUSparseMatrix->operator=(static_cast<GPUSparseMatrix<ElemType>&&>(*(moveFrom.m_GPUSparseMatrix)));
            else m_GPUSparseMatrix = new GPUSparseMatrix<ElemType>(static_cast<GPUSparseMatrix<ElemType>&&>(*(moveFrom.m_GPUSparseMatrix)))
            );                

        return *this;
    }

    template<class ElemType>
    void Matrix<ElemType>::Clear()
    {
        if (m_CPUMatrix!=NULL)
        {
            delete m_CPUMatrix;
            m_CPUMatrix = NULL;
        }
        if (m_GPUMatrix!=NULL)
        {
            delete m_GPUMatrix;
            m_GPUMatrix = NULL;
        }
        if (m_GPUSparseMatrix!=NULL)
        {
            delete m_GPUSparseMatrix;
            m_GPUSparseMatrix = NULL;
        }
        if (m_CPUSparseMatrix!=NULL)
        {
            delete m_CPUSparseMatrix;
            m_CPUSparseMatrix = NULL;
        }        

        m_matrixType=MatrixType::UNDETERMINED;
        m_currentDataLocation = CurrentDataLocation::NONE;
    }

    template<class ElemType>
    Matrix<ElemType>::~Matrix(void)
    {
        this->Clear();
    }

    template<class ElemType>
    Matrix<ElemType>  Matrix<ElemType>::Ones(const size_t rows, const size_t cols, short deviceId)
    {
        Matrix<ElemType> c(rows, cols, deviceId); //will initialize to 0
        c.SetValue(1);
        return c;
    }

    template<class ElemType>
    Matrix<ElemType>  Matrix<ElemType>::Zeros(const size_t rows, const size_t cols, short deviceId)
    {
        Matrix<ElemType> c(rows, cols, deviceId); //will initialize to 0
        c.SetValue(0);
        return c;
    }

    template<class ElemType>
    Matrix<ElemType>  Matrix<ElemType>::Eye(const size_t rows, short deviceId)
    {
        Matrix<ElemType> c(rows, rows, deviceId); //will initialize to 0
        c.SetDiagonalValue(1);
        return c;
    }

    template<class ElemType>
    Matrix<ElemType>  Matrix<ElemType>::RandomUniform(const size_t rows, const size_t cols, const ElemType low, const ElemType high, unsigned long seed, short deviceId)
    {
        Matrix<ElemType> c(rows, cols, deviceId); //will initialize to 0
        c.SetUniformRandomValue(low, high, seed);
        return c;
    }

    template<class ElemType>
    Matrix<ElemType>  Matrix<ElemType>::RandomGaussian(const size_t rows, const size_t cols, const ElemType mean, const ElemType sigma, unsigned long seed, short deviceId)
    {
        Matrix<ElemType> c(rows, cols, deviceId); //will initialize to 0
        c.SetGaussianRandomValue(mean, sigma, seed);
        return c;
    }


#pragma endregion Constructors, destructors and other static matrix builders

#pragma region Basic Operators

    template<class ElemType>
    void Matrix<ElemType>::ShiftBy(int numShift) 
    {            
        assert (numShift > 0);

        int devId = GetDeviceId();

        if (GetMatrixType() == MatrixType::DENSE)
        { 
                for (size_t i = this->GetNumCols()-1; i >= -numShift; i--)
                {
                    Matrix<ElemType> inp = this->ColumnSlice(i + numShift, 1);
                    Matrix<ElemType> out = this->ColumnSlice(i, 1) ; 
                    out = inp;
                }
                for (size_t i = 0; i < min(this->GetNumCols(), -numShift); i++)
                    this->ColumnSlice(i, 1).SetValue(0);
        }
        else if (GetMatrixType() == MatrixType::SPARSE)
        {
            if (devId == CPUDEVICE)
            {
                m_CPUSparseMatrix->ShiftBy(numShift);
            }
            else
                NOT_IMPLEMENTED;
        }
        else 
        {
            throw std::runtime_error("Unknown matrix type");
        }
    }
     
    template<class ElemType>
    size_t Matrix<ElemType>::BufferSize() const 
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return m_baseMatrix->GetSizeAllocated()*sizeof(ElemType), 
            return m_baseMatrix->GetSizeAllocated()*sizeof(ElemType), 
            return m_CPUSparseMatrix->BufferSize(), 
            return m_GPUSparseMatrix->BufferSize()
            );
        }

    template<class ElemType>
    ElemType* Matrix<ElemType>::BufferPointer() const 
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return m_baseMatrix->GetArray(), 
            return m_baseMatrix->GetArray(), 
            return m_CPUSparseMatrix->BufferPointer(), 
            return (ElemType*)m_GPUSparseMatrix->BufferPointer()
            );
        }

    template<class ElemType>
    size_t Matrix<ElemType>::NzCount() const {return m_baseMatrix->NzCount();}

    template<class ElemType>
    ElemType* Matrix<ElemType>::CopyToArray() const
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return m_CPUMatrix->CopyToArray(), 
            return m_GPUMatrix->CopyToArray(), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
    }

    //memory will be allocated by the callee if not enough but need to be deleted by the caller after it's done
    //return number of elements copied
    template<class ElemType>
    size_t  Matrix<ElemType>::CopyToArray(ElemType*& arrayCopyTo, size_t& currentArraySize) const
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return m_CPUMatrix->CopyToArray(arrayCopyTo, currentArraySize), 
            return m_GPUMatrix->CopyToArray(arrayCopyTo, currentArraySize), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
    }

    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::ColumnSlice(size_t startColumn, size_t numCols) const
    {            
        int devId = GetDeviceId();

        Matrix<ElemType> slice(matrixFlagDontOwnBuffer, (short)devId); //

        slice.m_preferredDeviceId = m_preferredDeviceId;

        if (GetMatrixType() == MatrixType::DENSE)
        { 
            if (devId == CPUDEVICE)
            {   
                if (slice.m_CPUMatrix != nullptr)
                    slice.m_CPUMatrix->operator=(static_cast<CPUMatrix<ElemType>&&>(m_CPUMatrix->ColumnSlice(startColumn, numCols)));
                else
                    slice.m_CPUMatrix = new CPUMatrix<ElemType>(static_cast<CPUMatrix<ElemType>&&>(m_CPUMatrix->ColumnSlice(startColumn, numCols)));
                slice.SetDataLocation(CPU, DENSE);
            }
            else
            {            
                if (slice.m_GPUMatrix != nullptr) 
                    slice.m_GPUMatrix->operator=(static_cast<GPUMatrix<ElemType>&&>(m_GPUMatrix->ColumnSlice(startColumn, numCols)));
                else
                    slice.m_GPUMatrix = new GPUMatrix<ElemType>(static_cast<GPUMatrix<ElemType>&&>(m_GPUMatrix->ColumnSlice(startColumn, numCols)));
                slice.SetDataLocation(GPU, DENSE);
            }
        }
        else if (GetMatrixType() == MatrixType::SPARSE)
        {
            NOT_IMPLEMENTED;
        }
        else 
        {
            throw std::runtime_error("Unknown matrix type");
        }

        return slice;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignColumnSlice(const Matrix<ElemType>& fromMatrix, size_t startColumn, size_t numCols)
    {            
        Clear();
        m_preferredDeviceId = fromMatrix.m_preferredDeviceId;

        DISPATCH_MATRIX_ON_FLAG(&fromMatrix,
            this,
            if (m_CPUMatrix != nullptr) m_CPUMatrix->AssignColumnSlice(*fromMatrix.m_CPUMatrix, startColumn, numCols);
            else m_CPUMatrix = new CPUMatrix<ElemType>(static_cast<CPUMatrix<ElemType>&&>(fromMatrix.m_CPUMatrix->ColumnSlice(startColumn, numCols))), 

            if (m_GPUMatrix != nullptr) m_GPUMatrix->AssignColumnSlice(*fromMatrix.m_GPUMatrix, startColumn, numCols);
            else  m_GPUMatrix = new GPUMatrix<ElemType>(static_cast<GPUMatrix<ElemType>&&>(fromMatrix.m_GPUMatrix->ColumnSlice(startColumn, numCols))), 

            NOT_IMPLEMENTED, 

            NOT_IMPLEMENTED
            );

        return *this;
    }


    //this function will change the matrix type between DENSE and SPARSE. 
    //WARNING: The correct implementation is to copy the matrix between DENSE and SPARSE
    //         However, the convertion functions are not implemented yet and so it will always create 
    //         a new blank matrix and destroy all info in the original matrix if different matrix type is asked. 
    template<class ElemType>
    void Matrix<ElemType>::SwitchToMatrixType(MatrixType newMatrixType, MatrixFormat newMatrixFormat)
    {
        if (m_matrixType==newMatrixType)
            return;

        if (GetDeviceId() == MANAGEDEXTERN)
        {
            return;
        }

        if (GetDeviceId()<0) //CPU
        {
            if (newMatrixType==MatrixType::SPARSE)
            {
                if (m_CPUSparseMatrix == nullptr)
                {
                    m_CPUSparseMatrix = new CPUSparseMatrix<ElemType>(newMatrixFormat); 
                    delete m_CPUMatrix;
                    m_CPUMatrix = nullptr;
                }
                SetDataLocation(CPU, SPARSE);
            }
            else if (newMatrixType==MatrixType::DENSE)
            {
                if (m_CPUMatrix == nullptr)
                {
                    m_CPUMatrix = new CPUMatrix<ElemType>(); 
                    delete m_CPUSparseMatrix;
                    m_CPUSparseMatrix = nullptr;
                }
                SetDataLocation(CPU, DENSE);
            }
            else
                throw std::runtime_error("Wrong new matrix type");
        }
        else //GPU
        {
            if (newMatrixType==MatrixType::SPARSE)
            {
                if (m_GPUSparseMatrix == nullptr)
                {
                    if (m_GPUMatrix == nullptr)
                    {
                        m_GPUSparseMatrix = new GPUSparseMatrix<ElemType>(newMatrixFormat, GetDeviceId());                        
                    }
                    else 
                    {
                        // Ideally the following two cases should be combined. The else case is legacy code
                        // and it is used for the legacy unit tests.
                        if (m_GPUMatrix->GetNumElements() == 0)
                        {
                            m_GPUSparseMatrix = new GPUSparseMatrix<ElemType>(newMatrixFormat, GetDeviceId());
                        } 
                        else
                        {
                            m_GPUSparseMatrix = new GPUSparseMatrix<ElemType>(*m_GPUMatrix); //this is deep copy in legacy code
                        }
                        delete m_GPUMatrix;
                        m_GPUMatrix = nullptr;
                    }
                }
                SetDataLocation(GPU, SPARSE);
            }
            else if (newMatrixType==MatrixType::DENSE)
            {
                if (m_GPUMatrix == nullptr)
                {
                    if (m_GPUSparseMatrix != nullptr)
                    {
                        m_GPUMatrix = new GPUMatrix<ElemType>(m_GPUSparseMatrix->CopyToDenseMatrix());
                        delete m_GPUSparseMatrix;
                        m_GPUSparseMatrix = nullptr;
                    }
                    else
                    {
                        m_GPUMatrix = new GPUMatrix<ElemType>(GetDeviceId());
                    }
                }
                SetDataLocation(GPU, DENSE);
            }
            else
                throw std::runtime_error("Wrong new matrix type");
        }
    }


    template<class ElemType>
    ElemType Matrix<ElemType>::Get00Element() const
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return this->m_CPUMatrix->Get00Element(), 
            return this->m_GPUMatrix->Get00Element(), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
            }

    template<class ElemType>
    const ElemType Matrix<ElemType>::operator() (const size_t row, const size_t col) const
    {
        DISPATCH_MATRIX_ON_FLAG_USECPU_4BOTH(this,
            nullptr,
            return m_CPUMatrix->operator()(row,col), 
            _transferFromDeviceToDevice(GetDeviceId(), CPUDEVICE, false); return m_CPUMatrix->operator()(row,col), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
        }


    //WARNING: This function is very slow for GPUs since it requires copying values between CPUs and GPUs. 
    //In addition, if ColumnSlice is used after this function but before the values are copied back to GPU
    //the operation will fail since the memory is not managed by the slice.
    //If you don't need to modify the values, please make sure to call the const version above.
    template<class ElemType>
    ElemType& Matrix<ElemType>::operator() (const size_t row, const size_t col)
    {    
        DISPATCH_MATRIX_ON_FLAG_USECPU_4BOTH(this,
            nullptr,
            return m_CPUMatrix->operator()(row,col), 
            _transferFromDeviceToDevice(GetDeviceId(), CPUDEVICE, false); SetDataLocation(CPU, DENSE); return m_CPUMatrix->operator()(row,col), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
        }

    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::Transpose()
    {
        if (IsEmpty())
            throw std::logic_error("Transpose: Matrix is empty.");

        Matrix<ElemType> c(this->GetNumCols(), this->GetNumRows(), (short)this->GetDeviceId());
        c.AssignTransposeOf(*this);
        return c;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignTransposeOf (const Matrix<ElemType>& a)
    {
        DecideAndMoveToRightDevice(a, *this);
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignTransposeOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignTransposeOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

        return *this;
    }

    template<class ElemType>
    void Matrix<ElemType>::SetValue(const ElemType v)
    {
        if (IsEmpty())
            throw std::logic_error("SetValue: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->SetValue(v), 
            m_GPUMatrix->SetValue(v), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
        }

    template<class ElemType>
    void Matrix<ElemType>::SetValue(const DeviceBoundNumber<ElemType>& db_number)
    {
        if (IsEmpty())
            throw std::logic_error("SetValue: Matrix is empty.");        

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->SetValue(*db_number.ExposePointer2Value()), 
            if (GetDeviceId()!=db_number.GetDeviceId())
                throw std::runtime_error("Matrix and device bound number must be on the same device");
            m_GPUMatrix->SetValue(db_number.ExposePointer2Value()), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
            }

    template<class ElemType>
    void Matrix<ElemType>::SetColumn(const ElemType* colPointer, size_t colInd)
    {
        if (colPointer == nullptr)
            throw std::invalid_argument("SetColumn: colPointer is null.");    

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->SetColumn(colPointer,colInd), 
            this->m_GPUMatrix->SetColumn(colPointer,colInd), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
        }

    template<class ElemType>
    void Matrix<ElemType>::SetColumn(const ElemType val, size_t colInd)
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->SetColumn(val,colInd), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED);
        }

    template<class ElemType>
    void Matrix<ElemType>::SetColumn(const Matrix<ElemType>& colMat, size_t colInd)
    {
        DecideAndMoveToRightDevice(*this, colMat);

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->SetColumn(*colMat.m_CPUMatrix,colInd), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED);
        }


    template<class ElemType>
    void Matrix<ElemType>::SetValue(const Matrix<ElemType>& deepCopyFrom)
    {
        if (this == &deepCopyFrom)
            return;

        this->m_preferredDeviceId = deepCopyFrom.m_preferredDeviceId;
        DecideAndMoveToRightDevice(deepCopyFrom, *this);
        this->SwitchToMatrixType(deepCopyFrom.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&deepCopyFrom,
            this,
            this->m_CPUMatrix->SetValue(*deepCopyFrom.m_CPUMatrix), 
            this->m_GPUMatrix->SetValue(*deepCopyFrom.m_GPUMatrix), 
            this->m_CPUSparseMatrix->SetValue(*deepCopyFrom.m_CPUSparseMatrix), 
            NOT_IMPLEMENTED
            );
            }


    //WARNING: what's the exact meaning of MANAGEDEXTERN here? This is not handled currently
    template<class ElemType>
    void Matrix<ElemType>::SetValue(const size_t numRows, const size_t numCols, ElemType *pArray, const size_t matrixFlags, int deviceId)
    {
        if (pArray == nullptr)
            throw std::invalid_argument("Invalid pArray.");

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->SetValue(numRows,numCols,pArray,matrixFlags), 
            m_GPUMatrix->SetValue(numRows,numCols,pArray,matrixFlags, deviceId), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
        }

    template<class ElemType>
    void Matrix<ElemType>::SetValue(const size_t rIdx, const size_t cIdx, ElemType val)
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            (*m_CPUMatrix)(rIdx, cIdx) = val, 
            NOT_IMPLEMENTED, 
            m_CPUSparseMatrix->SetValue(rIdx,cIdx,val), 
            NOT_IMPLEMENTED
            );
    }

    // read features
    template<class ElemType>
    void Matrix<ElemType>::SetMatrixFromCSCFormat(size_t *h_row, size_t *h_rowIdx, size_t size, size_t blockSize)
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            m_GPUSparseMatrix->SetMatrixFromCSCFormat(h_row, h_rowIdx, size, blockSize)
            );

    }
    
    // read labels
    template<class ElemType>
    void Matrix<ElemType>::SetMatrixFromLabelAndClass(size_t *h_row, size_t *h_block2Id, size_t *h_block2UniqId, size_t labelSize, size_t expandedSize, size_t blockSize)
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            m_GPUSparseMatrix->SetMatrixFromLabelAndClass(h_row, h_block2Id, h_block2UniqId, labelSize, expandedSize, blockSize)
            );

    }

    template<class ElemType>
    void Matrix<ElemType>::SetDiagonalValue(const ElemType v)
    {
        if (IsEmpty())
            throw std::logic_error("SetDiagonalValue: Matrix is empty.");

        if (GetNumRows() != GetNumCols())
            throw std::logic_error("SetDiagonalValue: NumRows and NumCols do not agree.");

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->SetDiagonalValue(v), 
            m_GPUMatrix->SetDiagonalValue(v), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
        }

    template<class ElemType>
    void Matrix<ElemType>::SetDiagonalValue(Matrix<ElemType>& vector)
    {
        if (IsEmpty() || vector.IsEmpty())
            throw std::logic_error("SetDiagonalValue: Matrix is empty.");

        if (GetNumRows() != GetNumCols())
            throw std::logic_error("SetDiagonalValue: NumRows and NumCols do not agree.");

        if (vector.GetNumRows() != 1 && vector.GetNumCols() != 1)
            throw std::logic_error("SetDiagonalValue: input vector must be a vector.");

        DecideAndMoveToRightDevice(*this, vector);

        if (vector.GetNumElements() == 1) //reduce to simple form
        {
            DISPATCH_MATRIX_ON_FLAG(&vector,
                nullptr,
                SetDiagonalValue(vector(0,0)), 
                SetDiagonalValue(vector.m_GPUMatrix->Get00Element()), 
                SetDiagonalValue(vector(0,0)), 
                SetDiagonalValue(vector.m_GPUMatrix->Get00Element())
                );
        }
        else if (vector.GetNumRows() != GetNumRows())
            throw std::logic_error("SetDiagonalValue: input vector's dimension does not agree with [this].");
        else
        {
            //WARNING: we use this pointer to decide which function to call. However, vector may be stored in a different matrix type (DENSE, SPARSE)
            DISPATCH_MATRIX_ON_FLAG(this,
                this,
                assert(vector.m_CPUMatrix != nullptr); m_CPUMatrix->SetDiagonalValue(*vector.m_CPUMatrix), 
                assert(vector.m_GPUMatrix != nullptr); m_GPUMatrix->SetDiagonalValue(*vector.m_GPUMatrix), 
                NOT_IMPLEMENTED, 
                NOT_IMPLEMENTED
                );
            }
            }

    template<class ElemType>
    void Matrix<ElemType>::SetUniformRandomValue(const ElemType low, const ElemType high, unsigned long seed)
    {
        if (IsEmpty())
            throw std::logic_error("SetUniformRandomValue: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->SetUniformRandomValue(low,high,seed), 
            m_GPUMatrix->SetUniformRandomValue(low,high,seed), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );     
        }

    template<class ElemType>
    void Matrix<ElemType>::SetGaussianRandomValue(const ElemType mean, const ElemType sigma, unsigned long seed)
    {
        if (sigma <= 0) 
            throw std::invalid_argument("SetUniformRandomValue: sigma must be a positive value.");

        if (IsEmpty())
            throw std::logic_error("SetUniformRandomValue: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->SetGaussianRandomValue(mean, sigma, seed), 
            m_GPUMatrix->SetGaussianRandomValue(mean, sigma, seed), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
        }

    template<class ElemType>
    void Matrix<ElemType>::AddGaussianRandomValue(const ElemType mean, const ElemType sigma, unsigned long seed)
    {
        if (sigma <= 0) 
            throw std::invalid_argument("SetUniformRandomValue: sigma must be a positive value.");

        if (IsEmpty())
            throw std::logic_error("SetUniformRandomValue: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->AddGaussianRandomValue(mean, sigma, seed), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );      
        }

    //maskRate: percentage of values masked out (similar to dropout rate)
    //scaleValue: which scale value to set to the left ones (unmasked items).
    template<class ElemType>
    void Matrix<ElemType>::SetUniformRandomMask(const ElemType maskRate, const ElemType scaleValue, unsigned long seed)
    {
        if (IsEmpty())
            throw std::logic_error("SetUniformRandomMask: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->SetUniformRandomMask(maskRate,scaleValue,seed), 
            m_GPUMatrix->SetUniformRandomMask(maskRate,scaleValue,seed), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );            
        }

    template<class ElemType>
    void Matrix<ElemType>::NormalGrad(Matrix<ElemType>& gradients, Matrix<ElemType>& functionValues, const ElemType learnRatePerSample, const ElemType momentum)
    {
        DecideAndMoveToRightDevice(*this, gradients, functionValues);

        DISPATCH_MATRIX_ON_FLAG(&gradients,
            nullptr,
            ScaleAndAdd((1-momentum) * learnRatePerSample, gradients, momentum, *this); functionValues -= *this, 
            ScaleAndAdd((1-momentum) * learnRatePerSample, gradients, momentum, *this); functionValues -= *this, 
            if (momentum != 0) gradients.m_CPUSparseMatrix->NormalGrad(*this->m_CPUMatrix, momentum); ScaleAndAdd(-learnRatePerSample, gradients, functionValues),
            if (momentum != 0) gradients.m_GPUSparseMatrix->NormalGrad(*this->m_GPUMatrix, momentum); ScaleAndAdd(-learnRatePerSample, gradients, functionValues)
            );
    }

    //both this and gradients will be changed
    template<class ElemType>
    void Matrix<ElemType>::Adagrad(Matrix<ElemType>& gradients)
    {
        DecideAndMoveToRightDevice(*this, gradients);

        DISPATCH_MATRIX_ON_FLAG(&gradients,
            &gradients,
            m_CPUMatrix->Adagrad(*gradients.m_CPUMatrix); SetDataLocation(CPU), 
            m_GPUMatrix->Adagrad(*gradients.m_GPUMatrix); SetDataLocation(GPU), 
            gradients.m_CPUSparseMatrix->Adagrad(*this->m_CPUMatrix); SetDataLocation(CPU), 
            NOT_IMPLEMENTED
            );
    }

    template<class ElemType>
    void Matrix<ElemType>::RmsProp(Matrix<ElemType>& gradients,
		ElemType RMS_GAMMA,
		ElemType RMS_WGT_INC,
		ElemType RMS_WGT_MAX,
		ElemType RMS_WGT_DEC,
		ElemType RMS_WGT_MIN
		)
    {
        DecideAndMoveToRightDevice(*this, gradients);

        DISPATCH_MATRIX_ON_FLAG(this,
            &gradients,
            m_CPUMatrix->RmsProp(*gradients.m_CPUMatrix, RMS_GAMMA, RMS_WGT_INC, RMS_WGT_MAX, RMS_WGT_DEC, RMS_WGT_MIN); SetDataLocation(CPU), 
            m_GPUMatrix->RmsProp(*gradients.m_GPUMatrix, RMS_GAMMA, RMS_WGT_INC, RMS_WGT_MAX, RMS_WGT_DEC, RMS_WGT_MIN); SetDataLocation(GPU),
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
    }

    template<class ElemType>
    void Matrix<ElemType>::Reshape(const size_t numRows, const size_t numCols)
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->Reshape(numRows,numCols), 
            m_GPUMatrix->Reshape(numRows,numCols), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
    }

    template<class ElemType>
    void Matrix<ElemType>::Resize(const size_t numRows, const size_t numCols, bool growOnly /*=true*/)
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            m_CPUMatrix->Resize(numRows,numCols,growOnly), 
            m_GPUMatrix->Resize(numRows,numCols,growOnly), 
            m_CPUSparseMatrix->Resize(numRows,numCols, numRows * numCols), 
            m_GPUSparseMatrix->Resize(numRows,numCols)
            );
    }

    template<class ElemType>
    void Matrix<ElemType>::Resize(const size_t numRows, const size_t numCols, const size_t allocatedSize)
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            m_CPUSparseMatrix->Resize(numRows,numCols, allocatedSize), 
            m_GPUSparseMatrix->Resize(numRows,numCols, allocatedSize)
            );
    }

    template<class ElemType>
    size_t Matrix<ElemType>::GetAllocatedSize() const
    {
        return m_baseMatrix->GetSizeAllocated();
    }
    

    //Reset for sparse matrix
    template<class ElemType>
    void Matrix<ElemType>::Reset()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            m_CPUSparseMatrix->Reset(), 
            m_GPUSparseMatrix->Reset()
            );
    }

    template<class ElemType> 
    size_t Matrix<ElemType>::GetNumRows() const
    {
        return m_baseMatrix->GetNumRows();
    }

    template<class ElemType>
    size_t Matrix<ElemType>::GetNumCols() const
    {
        return m_baseMatrix->GetNumCols();
    }

    template<class ElemType>
    size_t Matrix<ElemType>::GetNumElements() const
    {
        return GetNumRows()*GetNumCols();
    }

    template<class ElemType>
    bool Matrix<ElemType>::IsEmpty() const
    {
        return m_baseMatrix->IsEmpty();
    }

#pragma endregion Basic Operators

#pragma region Member BLAS Functions

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::operator+= (ElemType alpha) 
    {
        return AssignSumOf(alpha, *this);
    }

    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::operator+ (ElemType alpha) const
    {
        Matrix<ElemType> c(GetNumRows(), GetNumCols());
        c.AssignSumOf(alpha, *this);
        return c;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignSumOf(const ElemType alpha, const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignSumOf: Matrix a is empty.");        

        DecideAndMoveToRightDevice(a, *this);
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            m_CPUMatrix->AssignSumOf(alpha,*a.m_CPUMatrix), 
            m_GPUMatrix->AssignSumOf(alpha,*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

        return *this;
    }


    //if [this] and a have same dimension then [this]=[this]+a
    //if a is a column vector, add to all columns of [this] 
    //if a is a row vector, add to all rows of [this]
    //if a is a scalar, add it to all elements.
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::operator+= (const Matrix<ElemType>& a) 
    {
        DecideAndMoveToRightDevice(*this, a);

        if(!(GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->operator+=(*a.m_CPUMatrix), 
            this->m_GPUMatrix->operator+=(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED);

            return *this;
        }

    //if [this] and a have same dimension then OUTPUT=[this]+a
    //if a is a column vector, add to all columns of [this] 
    //if a is a row vector, add to all rows of [this]
    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::operator+ (const Matrix<ElemType>& a) const
    {
        if (GetNumElements() == 1)
        {
            Matrix<ElemType> c(a);

            DISPATCH_MATRIX_ON_FLAG(this,
                &c,
                c += (*this)(0,0), 
                c += (this->m_GPUMatrix->Get00Element()), 
                c += (*this)(0,0), 
                NOT_IMPLEMENTED
                );
            return c;
        }
        else if (a.GetNumElements() == 1)
        {
            Matrix<ElemType> c(*this);

            DISPATCH_MATRIX_ON_FLAG(&a,
                &c,
                c += a(0,0), 
                c += (a.m_GPUMatrix->Get00Element()), 
                c += a(0,0), 
                NOT_IMPLEMENTED
                );
            return c;
        }
        else
        {
            Matrix<ElemType> c(*this); //this implementation will introduce a copy overhead. but make resue of the code
            c += a;
            return c;
        }
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignSumOf(const Matrix<ElemType>& a, const Matrix<ElemType>& b)
    {
        if (a.GetNumElements() == 1)
        {
            SetValue(b);
            (*this) += a;
        }
        else
        {
            SetValue(a);
            (*this) += b;
        }
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::operator-= (ElemType alpha) 
    {
        return AssignDifferenceOf(*this, alpha);
    }

    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::operator- (ElemType alpha) const
    {
        Matrix<ElemType> c(GetNumRows(), GetNumCols());
        c.AssignDifferenceOf(*this, alpha);
        return c;
    }

        //for each column of a, we assign numRows starting from startIndex to this
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignRowSliceValuesOf(const Matrix<ElemType>& a, const size_t startIndex, const size_t numRows)
    {
        DecideAndMoveToRightDevice(a, *this);
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AssignRowSliceValuesOf(*a.m_CPUMatrix, startIndex, numRows), 
            this->m_GPUMatrix->AssignRowSliceValuesOf(*a.m_GPUMatrix, startIndex, numRows), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
        return *this;
    }

    //for the row slice of this starting from startIndex we add a to it.
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AddToRowSliceValuesOf(const Matrix<ElemType>& a, const size_t startIndex, const size_t numRows)
    {
        DecideAndMoveToRightDevice(*this, a);

        //WARNING: a and this must have same type
        if (!(GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AddToRowSliceValuesOf(*a.m_CPUMatrix, startIndex, numRows),
            this->m_GPUMatrix->AddToRowSliceValuesOf(*a.m_GPUMatrix, startIndex, numRows),
            NOT_IMPLEMENTED,
            NOT_IMPLEMENTED
            );

        return *this;
    }

    //for each column of this, we add row slice of a starting from startIndex
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AddWithRowSliceValuesOf(const Matrix<ElemType>& a, const size_t startIndex, const size_t numRows)
    {
        DecideAndMoveToRightDevice(*this, a);

        //WARNING: a and this must have same type
        if (! (GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AddWithRowSliceValuesOf(*a.m_CPUMatrix, startIndex, numRows),
            this->m_GPUMatrix->AddWithRowSliceValuesOf(*a.m_GPUMatrix, startIndex, numRows),
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>&  Matrix<ElemType>::AssignRepeatOf(const Matrix<ElemType>& a, const size_t numRowRepeats, const size_t numColRepeats)
    {
        DecideAndMoveToRightDevice(*this, a);

        //WARNING: a and this must have same type
        if (!(GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AssignRepeatOf(*a.m_CPUMatrix, numRowRepeats, numColRepeats),
            this->m_GPUMatrix->AssignRepeatOf(*a.m_GPUMatrix, numRowRepeats, numColRepeats),
            NOT_IMPLEMENTED,
            NOT_IMPLEMENTED
            );

        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignDifferenceOf(const ElemType alpha, const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignDifferenceOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AssignDifferenceOf(alpha,*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignDifferenceOf(alpha,*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignDifferenceOf(const Matrix<ElemType>& a, const ElemType alpha)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignDifferenceOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AssignDifferenceOf(*a.m_CPUMatrix, alpha), 
            this->m_GPUMatrix->AssignDifferenceOf(*a.m_GPUMatrix, alpha), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    //if [this] and a have same dimension then [this]=[this]-a
    //if a is a column vector, minus it from all columns of [this] 
    //if a is a row vector, minus it from all rows of [this]    
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::operator-= (const Matrix<ElemType>& a)
    {
		if (a.IsEmpty())
            throw std::logic_error("Minus Operation: Matrix a is empty.");
        DecideAndMoveToRightDevice(*this, a);

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            *this->m_CPUMatrix -= *a.m_CPUMatrix, 
            *this->m_GPUMatrix -= *a.m_GPUMatrix, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
		
        return *this;
    }

    //if [this] and a have same dimension then output=[this]-a
    //if a is a column vector, minus it from all columns of [this] 
    //if a is a row vector, minus it from all rows of [this]    
    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::operator- (const Matrix<ElemType>& a) const
    {
        Matrix<ElemType> c(*this); //this implementation will introduce a copy overhead. but make resue of the code
        ScaleAndAdd(-1,a,c);
        return c;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignDifferenceOf(const Matrix<ElemType>& a, const Matrix<ElemType>& b)
    {
        if (this != &a)
        {
            Resize(a.GetNumRows(), a.GetNumCols());
            SetValue(a);
        }
        (*this) -= b;
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::operator*= (ElemType alpha)
    {
        Scale(alpha, *this);
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::operator* (ElemType alpha) const
    {
        Matrix<ElemType> c(GetNumRows(), GetNumCols(), (short)this->m_preferredDeviceId);
        Scale(alpha, *this, c);
        return c;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignProductOf(const ElemType alpha, const Matrix<ElemType>& a)
    {
        Scale(alpha, a, *this);
        return *this;
    }

    // [this]=a*b
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignProductOf (const Matrix<ElemType>& a, const bool transposeA, const Matrix<ElemType>& b, const bool transposeB)
    {
        if (a.GetNumElements() == 1)
        {  
            if (transposeB)
                (*this)=AssignTransposeOf(b);
            else
                (*this)=b;

            DISPATCH_MATRIX_ON_FLAG(this,
                nullptr,
                (*this) *= a(0,0), 
                (*this) *= a.m_GPUMatrix->Get00Element(), 
                (*this) *= a(0,0), 
                NOT_IMPLEMENTED
                );
                
        }
        else if (b.GetNumElements() == 1)
        { 
            if (transposeA)
                (*this)=AssignTransposeOf(a);
            else
                (*this)=a;

            DISPATCH_MATRIX_ON_FLAG(this,
                nullptr,
                (*this) *= b(0,0), 
                (*this) *= b.m_GPUMatrix->Get00Element(), 
                (*this) *= b(0,0) ,
                NOT_IMPLEMENTED
                );                
        }
        else
            Multiply(a, transposeA, b, transposeB, *this);

        return *this;
    }

    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::operator* (const Matrix<ElemType>& a) const
    {        
        if (GetNumElements() == 1)
        {
            Matrix<ElemType> c((short)a.GetPreferredDeviceId());

            DISPATCH_MATRIX_ON_FLAG(this,
                nullptr,
                c.AssignProductOf((*this)(0,0), a), 
                c.AssignProductOf(this->m_GPUMatrix->Get00Element(), a), 
                c.AssignProductOf((*this)(0,0), a), 
                NOT_IMPLEMENTED
                );
                
            return c;
        }
        else if (a.GetNumElements() == 1)
        {
            Matrix<ElemType> c((short)GetPreferredDeviceId());

            DISPATCH_MATRIX_ON_FLAG(&a,
                nullptr,
                c.AssignProductOf(a(0,0), (*this)), 
                c.AssignProductOf(a.m_GPUMatrix->Get00Element(), (*this)), 
                c.AssignProductOf(a(0,0), (*this)), 
                NOT_IMPLEMENTED
                );
                
            return c;
        }
        else
        {
            Matrix<ElemType> c(this->GetNumRows(), a.GetNumCols(), (short)GetPreferredDeviceId());
            Multiply(*this, a, c);
            return c;
        }
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::operator/= (ElemType alpha)
    {
        (*this) *= 1/alpha;
        return (*this);
    }

    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::operator/ (ElemType alpha) const
    {
        return ((*this) * (1/alpha));
    }

    //element-wise power
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::operator^= (ElemType alpha)
    {
        auto& us = *this;
        ElementWisePower(alpha, us, us);
        return us;
    }

    //element-wise power
    template<class ElemType>
    Matrix<ElemType> Matrix<ElemType>::operator^ (ElemType alpha) const
    {
        Matrix<ElemType> c(GetNumRows(), GetNumCols(), (short)GetDeviceId());
        ElementWisePower(alpha, *this, c);
        return c;
    }


    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignElementPowerOf(const Matrix<ElemType>& a, const ElemType power)
    {
        ElementWisePower(power, a, *this);
        return *this;
    }

    //[this]=[this] .* a (we cannot override operator .* in c++)
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::ElementMultiplyWith (const Matrix<ElemType>& a)
    {
        return AssignElementProductOf(*this, a);
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::ElementDivideBy(const Matrix<ElemType>& a)
    {
        return AssignElementDivisionOf(*this, a);
    }

    //[this]=a .* b
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignElementProductOf (const Matrix<ElemType>& a, const Matrix<ElemType>& b)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("AssignElementProductOf: Matrix is empty.");

        assert (a.GetNumRows() == b.GetNumRows() && a.GetNumCols() == b.GetNumCols());
        if (!(a.GetNumRows() == b.GetNumRows() && a.GetNumCols() == b.GetNumCols()))
            throw std::invalid_argument("The input matrix dimensions do not match.");

        DecideAndMoveToRightDevice(a, b, *this);
        if (!(a.GetMatrixType() == b.GetMatrixType()))
            NOT_IMPLEMENTED;

        this->SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AssignElementProductOf(*a.m_CPUMatrix,*b.m_CPUMatrix), 
            this->m_GPUMatrix->AssignElementProductOf(*a.m_GPUMatrix,*b.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AddElementProductOf (const Matrix<ElemType>& a, const Matrix<ElemType>& b)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("AddElementProductOf: Matrix is empty.");

        assert (a.GetNumRows() == b.GetNumRows() && a.GetNumCols() == b.GetNumCols());
        if (!(a.GetNumRows() == b.GetNumRows() && a.GetNumCols() == b.GetNumCols()))
            throw std::invalid_argument("The input matrix dimensions do not match.");

        if (!(a.GetNumRows() == GetNumRows() && a.GetNumCols() == GetNumCols()))
            throw std::invalid_argument("The input matrix dimensions do not match [this].");

        DecideAndMoveToRightDevice(*this, a, b);

        if (! (a.GetMatrixType() == b.GetMatrixType() && GetMatrixType() == b.GetMatrixType())) 
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            this->m_CPUMatrix->AddElementProductOf(*a.m_CPUMatrix,*b.m_CPUMatrix), 
            this->m_GPUMatrix->AddElementProductOf(*a.m_GPUMatrix,*b.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

        return *this;
    }

    //[this]=a ./ b
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignElementDivisionOf (const Matrix<ElemType>& a, const Matrix<ElemType>& b)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("AssignElementDivisionOf: Matrix is empty.");

        assert (a.GetNumRows() == b.GetNumRows() && a.GetNumCols() == b.GetNumCols());
        if (!(a.GetNumRows() == b.GetNumRows() && a.GetNumCols() == b.GetNumCols()))
            throw std::invalid_argument("The input matrix dimensions do not match.");

        DecideAndMoveToRightDevice(a, b, *this);
        //WARNING: a and b must have same type
        if (!(a.GetMatrixType() == b.GetMatrixType()))
            NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AssignElementDivisionOf(*a.m_CPUMatrix,*b.m_CPUMatrix), 
            this->m_GPUMatrix->AssignElementDivisionOf(*a.m_GPUMatrix,*b.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }


    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::ColumnElementMultiplyWith(const Matrix<ElemType>& a)
    {
        if (a.IsEmpty() || IsEmpty())
            throw std::logic_error("ColumnElementMultiplyWith: Matrix is empty.");

        if (!(a.GetNumRows() == GetNumRows() && a.GetNumCols() == 1))
            throw std::invalid_argument("ColumnElementMultiplyWith: The input matrix should be a col vector and match [this]'s rows.");

        DecideAndMoveToRightDevice(*this, a);
        //WARNING: a and this must have same type
        if (!(GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->ColumnElementMultiplyWith(*a.m_CPUMatrix), 
            this->m_GPUMatrix->ColumnElementMultiplyWith(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::RowElementMultiplyWith(const Matrix<ElemType>& a)
    {
        if (a.IsEmpty() || IsEmpty())
            throw std::logic_error("RowElementMultiplyWith: Matrix is empty.");

        if (!(a.GetNumCols() == GetNumCols() && a.GetNumRows() == 1))
            throw std::invalid_argument("RowElementMultiplyWith: The input matrix should be a row vector and match [this]'s columns.");

        //WARNING: a and this must have same type
        if (! (GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());
       
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->RowElementMultiplyWith(*a.m_CPUMatrix), 
            this->m_GPUMatrix->RowElementMultiplyWith(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::RowElementDivideBy(const Matrix<ElemType>& a)
    {
        if (a.IsEmpty() || IsEmpty())
            throw std::logic_error("RowElementDivideBy: Matrix is empty.");

        if (!(a.GetNumCols() == GetNumCols() && a.GetNumRows() == 1))
            throw std::invalid_argument("RowElementDivideBy: The input matrix should be a row vector and match [this]'s columns.");

        //WARNING: a and this must have same type
        if (!(GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->RowElementDivideBy(*a.m_CPUMatrix),
            this->m_GPUMatrix->RowElementDivideBy(*a.m_GPUMatrix),
            NOT_IMPLEMENTED,
            NOT_IMPLEMENTED
            );

        return *this;
    }


    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::ColumnElementDivideBy(const Matrix<ElemType>& a)
    {
        if (a.IsEmpty() || IsEmpty())
            throw std::logic_error("ColumnElementDivideBy: Matrix is empty.");

        if (!(a.GetNumRows() == GetNumRows() && a.GetNumCols() == 1))
            throw std::invalid_argument("ColumnElementDivideBy: The input matrix should be a col vector and match [this]'s rows.");

        DecideAndMoveToRightDevice(*this, a);
        //WARNING: a and this must have same type
        if (!(GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->ColumnElementDivideBy(*a.m_CPUMatrix), 
            this->m_GPUMatrix->ColumnElementDivideBy(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }


    //[this]=1 ./ a
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::ElementInverse ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->ElementInverse(), 
            this->m_GPUMatrix->ElementInverse(), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->ElementInverse()
            );
                
        return (*this);
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignElementInverseOf (const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignElementInverseOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);        
        this->SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignElementInverseOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignElementInverseOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignElementInverseOf(*a.m_GPUSparseMatrix)
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceSigmoid ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceSigmoid(), 
            this->m_GPUMatrix->InplaceSigmoid(), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->InplaceSigmoid()
            );
                
        return (*this);
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignSigmoidOf (const Matrix<ElemType>& a)
    {
        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignSigmoidOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignSigmoidOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignSigmoidOf(*a.m_GPUSparseMatrix)
            );
                
        return *this;
    }

    //[this]=sigmoid([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceLinearRectifierDerivative ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceLinearRectifierDerivative(), 
            this->m_GPUMatrix->InplaceLinearRectifierDerivative(), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->InplaceLinearRectifierDerivative()
            );
                
        return (*this);
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignLinearRectifierDerivativeOf (const Matrix<ElemType>& a)
    {
        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignLinearRectifierDerivativeOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignLinearRectifierDerivativeOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignLinearRectifierDerivativeOf(*a.m_GPUSparseMatrix)
            );
                
        return *this;
    }

    //[this]=sigmoid([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceSigmoidDerivative ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceSigmoidDerivative(), 
            this->m_GPUMatrix->InplaceSigmoidDerivative(), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return (*this);
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignSigmoidDerivativeOf (const Matrix<ElemType>& a)
    {
        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignSigmoidDerivativeOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignSigmoidDerivativeOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignNumOfDiff (const Matrix<ElemType>& a, const Matrix<ElemType>& b)
    {
        DecideAndMoveToRightDevice(a, b, *this);        
        //WARNING: a and b must have same type
        if (!(a.GetMatrixType() == b.GetMatrixType()))
                NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AssignNumOfDiff(*a.m_CPUMatrix, *b.m_CPUMatrix), 
            this->m_GPUMatrix->AssignNumOfDiff(*a.m_GPUMatrix, *b.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }
    //[this]=tanh([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceTanh ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceTanh(), 
            this->m_GPUMatrix->InplaceTanh(), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->InplaceTanh()
            );
                
        return (*this);        
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignTanhOf (const Matrix<ElemType>& a)
    {
        DecideAndMoveToRightDevice(a, *this);    
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignTanhOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignTanhOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignTanhOf(*a.m_GPUSparseMatrix)
            );
                
        return *this;
    }

    //[this]=softmax([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceLogSoftmax (const bool isColWise)
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceLogSoftmax(isColWise), 
            this->m_GPUMatrix->InplaceLogSoftmax(isColWise), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;        
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignLogSoftmaxOf (const Matrix<ElemType>& a, const bool isColWise)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignLogSoftmaxOf: Matrix a is empty.");
        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignLogSoftmaxOf(*a.m_CPUMatrix,isColWise), 
            this->m_GPUMatrix->AssignLogSoftmaxOf(*a.m_GPUMatrix,isColWise), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceSqrt ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceSqrt(), 
            this->m_GPUMatrix->InplaceSqrt(), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->InplaceSqrt()
            );
                
        return *this;        
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignSqrtOf (const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignSqrtOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignSqrtOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignSqrtOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignSqrtOf(*a.m_GPUSparseMatrix)
            );
                
        return *this;
    }

    //[this]=exp([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceExp ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceExp(), 
            this->m_GPUMatrix->InplaceExp(), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->InplaceExp()
            );
                
        return *this;        
    }


    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignExpOf (const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignExpOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignExpOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignExpOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignExpOf(*a.m_GPUSparseMatrix)
            );
                
        return *this;
    }

    //[this]=exp([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceAbs ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            this->m_CPUMatrix->InplaceAbs(), 
            this->m_GPUMatrix->InplaceAbs(), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->InplaceAbs()
            );
                
        return *this;        
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignAbsOf (const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignAbsOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignAbsOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignAbsOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignAbsOf(*a.m_GPUSparseMatrix)
            );
                
        return *this;
    }

    //[this]=log([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceLog ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceLog(), 
            this->m_GPUMatrix->InplaceLog(), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->InplaceLog()
            );
                
        return *this;           
    }

    //[this]=log([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceLog10 ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceLog10(), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;           
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignLogOf (const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignLogOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignLogOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignLogOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignLogOf(*a.m_GPUSparseMatrix)
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignLog10Of (const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignLogOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignLog10Of(*a.m_CPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignLogOf(*a.m_GPUSparseMatrix)
            );
                
        return *this;
    }

    //[this]=cos([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceCosine ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceCosine(), 
            this->m_GPUMatrix->InplaceCosine(), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;           
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignCosineOf (const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignCosineOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignCosineOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignCosineOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    //[this]= -sin([this]) element wise
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceNegativeSine ()
    {
        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceNegativeSine(), 
            this->m_GPUMatrix->InplaceNegativeSine(), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;           
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignNegativeSineOf (const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignNegativeSineOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignNegativeSineOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignNegativeSineOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceTruncate(const ElemType threshold)
    {
        if (IsEmpty())
            throw std::logic_error("InplaceTruncateBottom: Matrix is empty.");

        if (sizeof(ElemType)==sizeof(float))
        {
            if (!isfinite((float)threshold))
                return *this;
        }
        else
        {
            if (!isfinite(threshold))
                return *this;
        }

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceTruncate(threshold), 
            this->m_GPUMatrix->InplaceTruncateTop(fabs(threshold)); this->m_GPUMatrix->InplaceTruncateBottom(-fabs(threshold)), 
            this->m_CPUSparseMatrix->InplaceTruncate(threshold),
            if(this->m_GPUSparseMatrix->m_legacy)
            {
                this->m_GPUSparseMatrix->InplaceTruncateTop(fabs(threshold));
                this->m_GPUSparseMatrix->InplaceTruncateBottom(-fabs(threshold));
            }
            else //new GPU Sparse matrix
            {
                this->m_GPUSparseMatrix->InplaceTruncate(threshold);
            }
            );

        return *this;
    }

    //Threshold truncating: this[i] = max( this[i], threshold )
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceTruncateBottom (const ElemType threshold)
    {
        if (IsEmpty())
            throw std::logic_error("InplaceTruncateBottom: Matrix is empty.");

        if (sizeof(ElemType)==sizeof(float))
        {
            if (!isfinite((float)threshold))
                return *this;
        }
        else
        {
            if (!isfinite(threshold))
                return *this;
        }

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceTruncateBottom(threshold), 
            this->m_GPUMatrix->InplaceTruncateBottom(threshold), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->InplaceTruncateBottom(threshold)
            );
                
        return *this;
    }

    //Threshold truncating: this[i] = max( a[i], threshold )
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignTruncateBottomOf (const Matrix<ElemType>& a, const ElemType threshold)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignTruncateBottomOf: Matrix a is empty.");

        if (sizeof(ElemType)==sizeof(float))
        {
		    if (!isfinite((float)threshold))
        {
                (*this) = a;
                return *this;
        }
            }
        else
        {
            if (!isfinite(threshold))
            {
                (*this) = a;
                return *this;
            }
            }

        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignTruncateBottomOf(*a.m_CPUMatrix, threshold), 
            this->m_GPUMatrix->AssignTruncateBottomOf(*a.m_GPUMatrix, threshold), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignTruncateBottomOf(*a.m_GPUSparseMatrix, threshold)
            );
                
        return *this;
    }

    //Threshold truncating: this[i] = min( this[i], threshold )
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::InplaceTruncateTop (const ElemType threshold)
    {
        if (IsEmpty())
            throw std::logic_error("InplaceTruncateTop: Matrix is empty.");

        if (sizeof(ElemType)==sizeof(float))
        {
            if (!isfinite((float)threshold))
                return *this;
            }
        else
        {
            if (!isfinite(threshold))
                return *this;
            }

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->InplaceTruncateTop(threshold), 
            this->m_GPUMatrix->InplaceTruncateTop(threshold), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->InplaceTruncateTop(threshold)
            );
                
        return *this;
    }
    //Threshold truncating: this[i] = min( a[i], threshold )
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignTruncateTopOf (const Matrix<ElemType>& a, const ElemType threshold)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignTruncateTopOf: Matrix a is empty.");

        if (sizeof(ElemType)==sizeof(float))
        {
            if (!isfinite((float)threshold))
            {
                (*this) = a;
                return *this;
            }
            }
        else
        {
            if (!isfinite(threshold))
            {
                (*this) = a;
                return *this;
            }
            }

        DecideAndMoveToRightDevice(a, *this);        
        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignTruncateTopOf(*a.m_CPUMatrix, threshold), 
            this->m_GPUMatrix->AssignTruncateTopOf(*a.m_GPUMatrix, threshold), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->AssignTruncateTopOf(*a.m_GPUSparseMatrix, threshold)
            );
                
        return *this;
    }

    //Threshold truncating: this[i] = 0 if abs(this[i]<threshold).
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::SetToZeroIfAbsLessThan (const ElemType threshold)
    {
        if (IsEmpty())
            throw std::logic_error("SetToZeroIfAbsLessThan: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->SetToZeroIfAbsLessThan(threshold), 
            this->m_GPUMatrix->SetToZeroIfAbsLessThan(threshold), 
            NOT_IMPLEMENTED, 
            this->m_GPUSparseMatrix->SetToZeroIfAbsLessThan(threshold)
            );
                
        return *this;
    }

        //sum of all elements
    template<class ElemType>
    ElemType Matrix<ElemType>::SumOfElements () const
    {
        if (IsEmpty())
            throw std::logic_error("SumOfElements: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return this->m_CPUMatrix->SumOfElements(), 
            return this->m_GPUMatrix->SumOfElements(), 
            NOT_IMPLEMENTED, 
            return this->m_GPUSparseMatrix->SumOfElements()
            );
                
            }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignSumOfElements(const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignSumOfElements: Matrix a is empty.");        

        //WARNING: a and this must have same type
        if (!(GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignSumOfElements(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignSumOfElements(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    DeviceBoundNumber<ElemType> Matrix<ElemType>::Sum_AsDeviceBoundNum() const
    {
                DeviceBoundNumber<ElemType> result;

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            ElemType* val = new ElemType; *val = this->m_CPUMatrix->SumOfElements(); result.ShallowCopyFrom(val,-1); return result, 
            return m_GPUMatrix->Sum_AsDeviceBoundNum(), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );    

        return result;
            }

    //sum of all elements
    template<class ElemType>
    ElemType Matrix<ElemType>::SumOfAbsElements () const
    {
        if (IsEmpty())
            throw std::logic_error("SumOfAbsElements: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return this->m_CPUMatrix->SumOfAbsElements(), 
            return this->m_GPUMatrix->SumOfAbsElements(), 
            NOT_IMPLEMENTED, 
            return this->m_GPUSparseMatrix->SumOfAbsElements()
            );                
            }

    template<class ElemType>
    bool Matrix<ElemType>::IsEqualTo(const Matrix<ElemType>& a, const ElemType threshold /*= 1e-8*/) const
    {
        return AreEqual(*this, a, threshold);
    }


    template<class ElemType>
    void Matrix<ElemType>::VectorNorm1(Matrix<ElemType>& c, const bool isColWise) const
    {
        if (IsEmpty())
            throw std::logic_error("VectorNormInf: Matrix is empty.");

        DecideAndMoveToRightDevice(*this, c);
        c.SwitchToMatrixType(GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            &c,
            this->m_CPUMatrix->VectorNorm1(*c.m_CPUMatrix,isColWise), 
            this->m_GPUMatrix->VectorNorm1(*c.m_GPUMatrix,isColWise), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );                
        }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignVectorNorm1Of(Matrix<ElemType>& a, const bool isColWise)
    {
        a.VectorNorm1(*this, isColWise);
        return *this;
    }

    template<class ElemType>
    void Matrix<ElemType>::VectorNorm2(Matrix<ElemType>& c, const bool isColWise) const
    {
        if (IsEmpty())
            throw std::logic_error("VectorNorm2: Matrix is empty.");

        DecideAndMoveToRightDevice(*this, c);
        c.SwitchToMatrixType(GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            &c,
            this->m_CPUMatrix->VectorNorm2(*c.m_CPUMatrix,isColWise), 
            this->m_GPUMatrix->VectorNorm2(*c.m_GPUMatrix,isColWise), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );                
        }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignVectorNorm2Of(Matrix<ElemType>& a, const bool isColWise)
    {
        a.VectorNorm2(*this, isColWise);
        return *this;
    }

    template<class ElemType>
    void Matrix<ElemType>::VectorNormInf(Matrix<ElemType>& c, const bool isColWise) const
    {
        if (IsEmpty())
            throw std::logic_error("VectorNormInf: Matrix is empty.");

        DecideAndMoveToRightDevice(*this, c);
        c.SwitchToMatrixType(GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            &c,
            this->m_CPUMatrix->VectorNormInf(*c.m_CPUMatrix,isColWise), 
            this->m_GPUMatrix->VectorNormInf(*c.m_GPUMatrix,isColWise), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );                
        }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignVectorNormInfOf(Matrix<ElemType>& a, const bool isColWise)
    {
        a.VectorNormInf(*this, isColWise);
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignInnerProductOf(const Matrix<ElemType>& a, const Matrix<ElemType>& b, const bool isColWise)
    {
        InnerProduct (a, b, *this,isColWise);
        return *this;
    }

    //column-wise crossproduct
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignKhatriRaoProductOf(const Matrix<ElemType>& a, const Matrix<ElemType>& b)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("AssignKhatriRaoProductOf: Matrix is empty.");

        assert (a.GetNumCols() == b.GetNumCols());
        if (!(a.GetNumCols() == b.GetNumCols()))
            throw std::invalid_argument("AssignKhatriRaoProductOf: The input matrix dimensions do not match.");

        DecideAndMoveToRightDevice(a, b, *this);
        //WARNING: a and b must have same type
        if (!(a.GetMatrixType() == b.GetMatrixType()))
            NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AssignKhatriRaoProductOf(*a.m_CPUMatrix,*b.m_CPUMatrix), 
            this->m_GPUMatrix->AssignKhatriRaoProductOf(*a.m_GPUMatrix,*b.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    //column-wise reshaped product. Used to compute KhatriRaoProduct Gradient
    //   this = reshape each column of a from (K1xK2,1) to (K1, K2) 
    //   if each column of a is not transposed, each (K1, K2) times each column of b (K2, frames).
    //   the output is a (K1, frames) matrix
    //   if each column of a is tranposed, each (K1, K2)^T times each column of b(K1, frames) and output is (K2, frames)
    //column-wise crossproduct
    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AddColumnReshapeProductOf(const Matrix<ElemType>& a, const Matrix<ElemType>& b, const bool transposeAColumn)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("AddColumnReshapeProductOf: Matrix is empty.");

        assert (a.GetNumCols() == b.GetNumCols());
        if (!(a.GetNumCols() == b.GetNumCols()))
            throw std::invalid_argument("AddColumnReshapeProductOf: The input matrix dimensions do not match.");

        DecideAndMoveToRightDevice(*this, a, b);
        //WARNING: a and b must have same type
        if (!(a.GetMatrixType() == b.GetMatrixType() && GetMatrixType() == b.GetMatrixType()))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
            this->m_CPUMatrix->AddColumnReshapeProductOf(*a.m_CPUMatrix,*b.m_CPUMatrix, transposeAColumn), 
            this->m_GPUMatrix->AddColumnReshapeProductOf(*a.m_GPUMatrix,*b.m_GPUMatrix, transposeAColumn), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AddWithScaleOf(ElemType alpha, const Matrix<ElemType>& a)
    {
        ScaleAndAdd(alpha, a, *this);
        return *this;
    }

    template<class ElemType>
    ElemType Matrix<ElemType>::FrobeniusNorm() const
    {
        if (IsEmpty())
            throw std::logic_error("FrobeniusNorm: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return this->m_CPUMatrix->FrobeniusNorm(), 
            return this->m_GPUMatrix->FrobeniusNorm(), 
            NOT_IMPLEMENTED, 
            return this->m_GPUSparseMatrix->FrobeniusNorm()
            );                
        }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignFrobeniusNormOf(const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignFrobeniusNormOf: Matrix a is empty.");

        this->Resize(1,1);

        //WARNING: a and this must have same type
        if (! (GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignFrobeniusNormOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignFrobeniusNormOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    ElemType Matrix<ElemType>::MatrixNormInf() const
    {
        if (IsEmpty())
            throw std::logic_error("MatrixNormInf: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return this->m_CPUMatrix->MatrixNormInf(), 
            return this->m_GPUMatrix->MatrixNormInf(), 
            NOT_IMPLEMENTED, 
            return this->m_GPUSparseMatrix->MatrixNormInf()
            );                
        }

    template<class ElemType>
    ElemType Matrix<ElemType>::MatrixNorm1() const
    {
        if (IsEmpty())
            throw std::logic_error("MatrixNorm1: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return this->m_CPUMatrix->MatrixNorm1(), 
            return this->m_GPUMatrix->MatrixNorm1(), 
            NOT_IMPLEMENTED, 
            return this->m_GPUSparseMatrix->MatrixNorm1()
            );
                
        }

    template<class ElemType>
    ElemType Matrix<ElemType>::MatrixNorm0() const
    {
        if (IsEmpty())
            throw std::logic_error("MatrixNorm0: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return this->m_CPUMatrix->MatrixNorm0(), 
            return this->m_GPUMatrix->MatrixNorm0(), 
            NOT_IMPLEMENTED, 
            return this->m_GPUSparseMatrix->MatrixNorm0()
            );               
        }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignSignOf(const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AssignSignOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);
        //WARNING: a and this must have same type
        if (!(GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AssignSignOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AssignSignOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AddSignOf(const Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("AddSignOf: Matrix a is empty.");

        DecideAndMoveToRightDevice(a, *this);
        if (!(GetMatrixType() == a.GetMatrixType()))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(&a,
            this,
            this->m_CPUMatrix->AddSignOf(*a.m_CPUMatrix), 
            this->m_GPUMatrix->AddSignOf(*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    //I decided to use Matrix<ElemType>& maxIndexes instead of integer vector because the result may be used to do additional calculation
    template<class ElemType>
    void Matrix<ElemType>::VectorMax(Matrix<ElemType>& maxIndexes, Matrix<ElemType>& maxValues, const bool isColWise) const
    {
        if (IsEmpty())
            throw std::logic_error("VectorMax: Matrix is empty.");

        DecideAndMoveToRightDevice(*this, maxIndexes, maxValues);
        maxIndexes.SwitchToMatrixType(GetMatrixType());
        maxValues.SwitchToMatrixType(GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            &maxValues,
            this->m_CPUMatrix->VectorMax(*maxIndexes.m_CPUMatrix,*maxValues.m_CPUMatrix,isColWise); maxIndexes.SetDataLocation(CPU, DENSE), 
            this->m_GPUMatrix->VectorMax(*maxIndexes.m_GPUMatrix,*maxValues.m_GPUMatrix,isColWise); maxIndexes.SetDataLocation(GPU, DENSE), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
               
        }

    template<class ElemType>
    void Matrix<ElemType>::VectorMin(Matrix<ElemType>& minIndexes, Matrix<ElemType>& minValues, const bool isColWise) const
    {
        if (IsEmpty())
            throw std::logic_error("VectorMin: Matrix is empty.");

        DecideAndMoveToRightDevice(*this, minIndexes, minValues);
        minIndexes.SwitchToMatrixType(GetMatrixType());
        minValues.SwitchToMatrixType(GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            &minValues,
            this->m_CPUMatrix->VectorMin(*minIndexes.m_CPUMatrix,*minValues.m_CPUMatrix,isColWise); minIndexes.SetDataLocation(CPU, DENSE), 
            this->m_GPUMatrix->VectorMin(*minIndexes.m_GPUMatrix,*minValues.m_GPUMatrix,isColWise); minIndexes.SetDataLocation(GPU, DENSE), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        }

#pragma endregion Member BLAS Functions

#pragma region Other helper Functions

    template<class ElemType>
    wchar_t* Matrix<ElemType>::GetMatrixName() const
    {
        return this->m_baseMatrix->GetMatrixName();
    }

    template<class ElemType>
    void Matrix<ElemType>::SetMatrixName(const wchar_t* s)
        {
        if (m_currentDataLocation==CurrentDataLocation::BOTH)
        {
            if (GetMatrixType() == MatrixType::DENSE)
            {
                this->m_CPUMatrix->SetMatrixName(s);
                this->m_GPUMatrix->SetMatrixName(s);
        }
            else if (GetMatrixType() == MatrixType::SPARSE)
            {
                this->m_CPUSparseMatrix->SetMatrixName(s);
                this->m_GPUSparseMatrix->SetMatrixName(s);
            }
        }
        else
        {
            DISPATCH_MATRIX_ON_FLAG(this,
                nullptr,
                this->m_CPUMatrix->SetMatrixName(s), 
                this->m_GPUMatrix->SetMatrixName(s), 
                this->m_CPUSparseMatrix->SetMatrixName(s), 
                this->m_GPUSparseMatrix->SetMatrixName(s)
                );
        }
    }

    template<class ElemType>
    int Matrix<ElemType>::GetDeviceId() const
    {
        if (m_currentDataLocation==CurrentDataLocation::NONE)
            return m_preferredDeviceId;

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            return CPUDEVICE, 
            return m_GPUMatrix->GetComputeDeviceId(), 
            return CPUDEVICE, 
            return m_GPUSparseMatrix->GetComputeDeviceId()
            );                
    }

    //if different and prefered devices are the same, move to preferred device. 
    //other wise GPU>CPU and if both are GPU move to a's preferred device
    template<class ElemType>
    void Matrix<ElemType>::DecideAndMoveToRightDevice(const Matrix<ElemType> &a, const Matrix<ElemType> &b, const Matrix<ElemType> &c)
        {
        int deviceIdA = a.GetDeviceId(), deviceIdB = b.GetDeviceId(), deviceIdC = c.GetDeviceId();
        if (deviceIdA == deviceIdB && deviceIdA == deviceIdC)
            return;

        int preferredDeviceIdA = a.GetPreferredDeviceId(), preferredDeviceIdB = b.GetPreferredDeviceId(), preferredDeviceIdC = c.GetPreferredDeviceId();

        if (preferredDeviceIdA == preferredDeviceIdB && preferredDeviceIdA == preferredDeviceIdC) //move to preferred
        {
            a._transferToDevice(preferredDeviceIdA);
            b._transferToDevice(preferredDeviceIdA);
            c._transferToDevice(preferredDeviceIdA);
        }
        else if (deviceIdB == deviceIdC && deviceIdB != CPUDEVICE)
        {
            a._transferToDevice(deviceIdB);
        }
        else if (deviceIdA != CPUDEVICE) //use it
        {
            b._transferToDevice(deviceIdA);
            c._transferToDevice(deviceIdA);
    }
        else if(deviceIdB != CPUDEVICE)
        {
            a._transferToDevice(deviceIdB);
            c._transferToDevice(deviceIdB);
        }
        else
        {
            a._transferToDevice(deviceIdC);
            b._transferToDevice(deviceIdC);
        }
    }

    //if different and prefered devices are the same, move to preferred device. 
    //other wise GPU>CPU and if both are GPU move to a's preferred device
    template<class ElemType>
    void Matrix<ElemType>::DecideAndMoveToRightDevice(const Matrix<ElemType> &a, const Matrix<ElemType> &b)
    {
        int deviceIdA = a.GetDeviceId(), deviceIdB = b.GetDeviceId();
        if (deviceIdA == deviceIdB)
            return;

        int preferredDeviceIdA = a.GetPreferredDeviceId(), preferredDeviceIdB = b.GetPreferredDeviceId();

        if (preferredDeviceIdA == preferredDeviceIdB) //move to preferred
        {
            a._transferToDevice(preferredDeviceIdA);
            b._transferToDevice(preferredDeviceIdA);
        }
        else if (deviceIdA != CPUDEVICE) //use it
            {
            b._transferToDevice(deviceIdA);
            }
            else 
            {                             
            a._transferToDevice(deviceIdB);
        }
    }

    template<class ElemType>
    void Matrix<ElemType>::_transferToDevice(int to_id, bool ismoved,bool emptyTransfer) const
    {
        int from_id = GetDeviceId();
        if (to_id == from_id)  //nothing to do
            return;

        if (this->OwnBuffer())
            _transferFromDeviceToDevice(from_id,  to_id, ismoved, emptyTransfer);
        else
            throw std::runtime_error("Cannot move externally owned matrices to the preferred device.");
    }

    template<class ElemType>
    void Matrix<ElemType>::_transferFromDeviceToDevice(int from_id, int to_id, bool ismoved,bool emptyTransfer) const
    {
        // if it's externally managed assume it's in the proper location
        if (from_id == MANAGEDEXTERN || to_id == MANAGEDEXTERN)
            return;

        if (from_id < 0) 
            from_id = CPUDEVICE;
        if (to_id < 0)
            to_id = CPUDEVICE;

        if (from_id == to_id)
        {
            if (from_id != GetDeviceId())
                throw std::runtime_error("Trying to transfer matrix from device to the same device while the matrix does not live in the from device.");
            
            return;
        }

        if (m_matrixType==MatrixType::SPARSE)
            NOT_IMPLEMENTED;

#pragma omp critical
        {
            if (from_id == CPUDEVICE) //from CPU to GPU
            {
                if (m_CPUMatrix==NULL)
                    throw std::logic_error("Can't move from CPU because I'm not there!");
                if (m_GPUMatrix!=NULL)
                    delete m_GPUMatrix;
                if (m_CPUMatrix->GetNumElements() !=0 && !emptyTransfer)
                {
                    m_GPUMatrix = new GPUMatrix<ElemType>(m_CPUMatrix->GetNumRows(), m_CPUMatrix->GetNumCols(), m_CPUMatrix->GetArray(), matrixFlagNormal,to_id);
                }
                else
                {
                    m_GPUMatrix = new GPUMatrix<ElemType>(to_id);
                }
                if (ismoved)
                {
                    delete m_CPUMatrix;
                    m_CPUMatrix=NULL;
                    SetDataLocation(GPU, DENSE);
                }
                else
                {
                    SetDataLocation(BOTH, DENSE);
                }
            }
            else //from GPU
            {
                if (m_GPUMatrix==NULL || m_GPUMatrix->GetComputeDeviceId()!=from_id)
                    throw std::logic_error("This matrix isn't on this (or any?) GPU");

                if (to_id < 0) //to CPU
                {
                    if (m_CPUMatrix!=NULL)
                        delete m_CPUMatrix;

                    if (m_GPUMatrix->GetNumElements() !=0 && !emptyTransfer)
                        {
                            ElemType *arr = m_GPUMatrix->CopyToArray();
                            m_CPUMatrix = new CPUMatrix<ElemType>(m_GPUMatrix->GetNumRows(), m_GPUMatrix->GetNumCols(), arr, matrixFlagNormal);
                            delete[] arr;
                        }
                        else
                        {
                            m_CPUMatrix = new CPUMatrix<ElemType>();
                        }

                    if (ismoved)
                    {
                        delete m_GPUMatrix;
                        m_GPUMatrix = NULL;
                        SetDataLocation(CPU, DENSE);
                    }
                    else
                    {
                        SetDataLocation(BOTH, DENSE);
                    }
                }
                else //to another GPU
                {
                    m_GPUMatrix->ChangeDeviceTo(to_id);
                }
            }
        }// and of omp critical section
    }

    template<class ElemType>
    void Matrix<ElemType>::TransferFromDeviceToDevice(int from_id, int to_id, bool ismoved, bool emptyTransfer, bool updatePreferredDevice) const
    {
        _transferFromDeviceToDevice(from_id,to_id,ismoved,emptyTransfer);
        if (updatePreferredDevice && m_preferredDeviceId != MANAGEDEXTERN)
            m_preferredDeviceId=GetDeviceId();
    }

    template<class ElemType>
    void Matrix<ElemType>::Print(const char* matrixName, size_t rowStart, size_t rowEnd, size_t colStart, size_t colEnd) const
    {
        if (IsEmpty())
            throw std::logic_error("Print: Matrix is empty.");

        DISPATCH_MATRIX_ON_FLAG(this,
            nullptr,
            this->m_CPUMatrix->Print(matrixName, rowStart, rowEnd, colStart, colEnd), 
            _transferToDevice(CPUDEVICE); this->m_CPUMatrix->Print(matrixName, rowStart, rowEnd, colStart, colEnd), 
            this->m_CPUSparseMatrix->Print(matrixName), 
            _transferToDevice(CPUDEVICE); this->m_CPUSparseMatrix->Print(matrixName)
            );
                
    }

    template<class ElemType>
    void Matrix<ElemType>::Print(const char* matrixName /*=nullptr*/) const
    {
        Print(matrixName, 0, GetNumRows()-1, 0, GetNumCols()-1);
    }

    //helpfer function used for convolution neural network 
    template<class ElemType>
    Matrix<ElemType>&   Matrix<ElemType>::AssignPackedConvolutionInput(const Matrix<ElemType>& inputSubBatch, 
        const size_t inputWidth, const size_t inputHeight, const size_t inputChannels,
        const size_t outputWidth, const size_t outputHeight, const size_t outputChannels,
        const size_t kernelWidth, const size_t kernelHeight, const size_t horizontalSubsample, const size_t verticalSubsample, 
        const bool zeroPadding)
    {
        DecideAndMoveToRightDevice(inputSubBatch, *this);        
        SwitchToMatrixType(inputSubBatch.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&inputSubBatch,
            this,
                m_CPUMatrix->AssignPackedConvolutionInput(*(inputSubBatch.m_CPUMatrix), 
                    inputWidth, inputHeight, inputChannels,
                    outputWidth, outputHeight, outputChannels,
                    kernelWidth, kernelHeight, horizontalSubsample, verticalSubsample, 
                    zeroPadding), 
                m_GPUMatrix->AssignPackedConvolutionInput(*(inputSubBatch.m_GPUMatrix), 
                    inputWidth, inputHeight, inputChannels,
                    outputWidth, outputHeight, outputChannels,
                    kernelWidth, kernelHeight, horizontalSubsample, verticalSubsample, 
                    zeroPadding), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return *this;
    }

    //helpfer function used for convolution neural network 
    template<class ElemType>
    Matrix<ElemType>&   Matrix<ElemType>::UnpackConvolutionInput(Matrix<ElemType>& inputSubBatch, 
        const size_t inputWidth, const size_t inputHeight, const size_t inputChannels,
        const size_t outputWidth, const size_t outputHeight, const size_t outputChannels,
        const size_t kernelWidth, const size_t kernelHeight, const size_t horizontalSubsample, const size_t verticalSubsample, 
        const bool zeroPadding) const
    {
        DecideAndMoveToRightDevice(*this, inputSubBatch);        
        inputSubBatch.SwitchToMatrixType(GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(this,
            &inputSubBatch,
                m_CPUMatrix->UnpackConvolutionInput(*(inputSubBatch.m_CPUMatrix), 
                    inputWidth, inputHeight, inputChannels,
                    outputWidth, outputHeight, outputChannels,
                    kernelWidth, kernelHeight, horizontalSubsample, verticalSubsample, 
                    zeroPadding), 
                m_GPUMatrix->UnpackConvolutionInput(*(inputSubBatch.m_GPUMatrix), 
                    inputWidth, inputHeight, inputChannels,
                    outputWidth, outputHeight, outputChannels,
                    kernelWidth, kernelHeight, horizontalSubsample, verticalSubsample, 
                    zeroPadding), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
        return inputSubBatch;
    }

    template<class ElemType>
    Matrix<ElemType>&   Matrix<ElemType>::AssignMaxPoolingResult(const Matrix<ElemType>& inputBatch, const size_t channels,
        const size_t inputWidth, const size_t inputHeight,  const size_t inputSizePerSample, 
        const size_t outputWidth, const size_t outputHeight, const size_t outputSizePerSample, 
        const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample)
    {
        DecideAndMoveToRightDevice(inputBatch, *this);        
        SwitchToMatrixType(inputBatch.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&inputBatch,
            this,
                m_CPUMatrix->AssignMaxPoolingResult(*(inputBatch.m_CPUMatrix), channels,
                    inputWidth, inputHeight,inputSizePerSample, 
                    outputWidth, outputHeight,  outputSizePerSample, 
                    windowWidth, windowHeight, horizontalSubsample, verticalSubsample), 
                m_GPUMatrix->AssignMaxPoolingResult(*(inputBatch.m_GPUMatrix), channels,
                    inputWidth, inputHeight, inputSizePerSample, 
                    outputWidth, outputHeight, outputSizePerSample, 
                    windowWidth, windowHeight, horizontalSubsample, verticalSubsample), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>&   Matrix<ElemType>::AddMaxPoolingGradient(const Matrix<ElemType>& outputGradientBatch, const Matrix<ElemType>& inputBatch, const Matrix<ElemType>& outputBatch, 
        const size_t channels, 
        const size_t inputWidth, const size_t inputHeight, const size_t inputSizePerSample, 
        const size_t outputWidth, const size_t outputHeight, const size_t outputSizePerSample, 
        const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample)
    {
        DecideAndMoveToRightDevice(*this, outputGradientBatch, inputBatch); 
        outputBatch._transferToDevice(GetDeviceId());

        if (!(GetMatrixType() == outputGradientBatch.GetMatrixType() 
            && GetMatrixType() == inputBatch.GetMatrixType()  
            && GetMatrixType() == outputBatch.GetMatrixType()))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
                m_CPUMatrix->AddMaxPoolingGradient(*(outputGradientBatch.m_CPUMatrix), *(inputBatch.m_CPUMatrix), *(outputBatch.m_CPUMatrix), channels, 
                    inputWidth, inputHeight, inputSizePerSample, 
                    outputWidth, outputHeight, outputSizePerSample, 
                    windowWidth, windowHeight, horizontalSubsample, verticalSubsample), 
                m_GPUMatrix->AddMaxPoolingGradient(*(outputGradientBatch.m_GPUMatrix), *(inputBatch.m_GPUMatrix), *(outputBatch.m_GPUMatrix), channels, 
                    inputWidth, inputHeight, inputSizePerSample, 
                    outputWidth, outputHeight, outputSizePerSample, 
                    windowWidth, windowHeight, horizontalSubsample, verticalSubsample);, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>&   Matrix<ElemType>::AssignAveragePoolingResult(const Matrix<ElemType>& inputBatch, const size_t channels,
        const size_t inputWidth, const size_t inputHeight,  const size_t inputSizePerSample, 
        const size_t outputWidth, const size_t outputHeight, const size_t outputSizePerSample, 
        const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample)
    {
        DecideAndMoveToRightDevice(inputBatch, *this);        
        SwitchToMatrixType(inputBatch.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&inputBatch,
            this,
                m_CPUMatrix->AssignAveragePoolingResult(*(inputBatch.m_CPUMatrix), channels,
                    inputWidth, inputHeight,inputSizePerSample, 
                    outputWidth, outputHeight,  outputSizePerSample, 
                    windowWidth, windowHeight, horizontalSubsample, verticalSubsample), 
                m_GPUMatrix->AssignAveragePoolingResult(*(inputBatch.m_GPUMatrix), channels,
                    inputWidth, inputHeight, inputSizePerSample, 
                    outputWidth, outputHeight, outputSizePerSample, 
                    windowWidth, windowHeight, horizontalSubsample, verticalSubsample), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

        return *this;
    }

    template<class ElemType>
    Matrix<ElemType>&   Matrix<ElemType>::AddAveragePoolingGradient(const Matrix<ElemType>& outputGradientBatch, 
        const size_t channels, 
        const size_t inputWidth, const size_t inputHeight, const size_t inputSizePerSample, 
        const size_t outputWidth, const size_t outputHeight, const size_t outputSizePerSample, 
        const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample)
    {
        DecideAndMoveToRightDevice(*this, outputGradientBatch);        
        if (!(GetMatrixType() == outputGradientBatch.GetMatrixType()))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(this,
            this,
                m_CPUMatrix->AddAveragePoolingGradient(*(outputGradientBatch.m_CPUMatrix), channels, 
                    inputWidth, inputHeight, inputSizePerSample, 
                    outputWidth, outputHeight, outputSizePerSample, 
                    windowWidth, windowHeight, horizontalSubsample, verticalSubsample), 
                m_GPUMatrix->AddAveragePoolingGradient(*(outputGradientBatch.m_GPUMatrix), channels, 
                    inputWidth, inputHeight, inputSizePerSample, 
                    outputWidth, outputHeight, outputSizePerSample, 
                    windowWidth, windowHeight, horizontalSubsample, verticalSubsample), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

        return *this;
    }


#pragma endregion Other Helper Functions

#pragma region Static BLAS Functions

    template<class ElemType>
    void Matrix<ElemType>::SVD(const Matrix<ElemType>& A, Matrix<ElemType>& SIGMA, Matrix<ElemType>& U, Matrix<ElemType>& VT)
    {
        if (A.IsEmpty() )
            throw std::logic_error("SVD:  the input matrix is empty.");        

        DecideAndMoveToRightDevice(A, SIGMA, U);    
        VT._transferToDevice(A.GetDeviceId());

        SIGMA.SwitchToMatrixType(A.GetMatrixType());
        U.SwitchToMatrixType(A.GetMatrixType());
        VT.SwitchToMatrixType(A.GetMatrixType());


        DISPATCH_MATRIX_ON_FLAG(&A,
            nullptr,
        Matrix<ElemType> tA = A;
                CPUMatrix<ElemType>::SVD(*tA.m_CPUMatrix, *SIGMA.m_CPUMatrix, *U.m_CPUMatrix, *VT.m_CPUMatrix);
                SIGMA.SetDataLocation(CPU);
                U.SetDataLocation(CPU);
                VT.SetDataLocation(CPU), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
    }

    /// <summary>Matrix-matrix multiply with col-major matrices (a and b may be transposed): c = alpha * op(a) * op(b) + beta*c</summary>
    /// <param name="alpha">Scalar</param>
    /// <param name="a">Input matrix</param>
    /// <param name="transposeA">Whether matrix a is transposed</param>
    /// <param name="b">Input matrix</param>
    /// <param name="transposeB">Whether matrix b is transposed</param>
    /// <param name="beta">Scalar</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>
    void Matrix<ElemType>::MultiplyAndWeightedAdd(ElemType alpha, const Matrix<ElemType>& a, const bool transposeA, const Matrix<ElemType>& b, const bool transposeB, 
        ElemType beta, Matrix<ElemType>& c)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("MultiplyAndWeightedAdd:  one of the input matrix is empty.");        

        DecideAndMoveToRightDevice(a,b,c);        

        if (c.GetDeviceId()<0) //CPU
        {
            if (a.GetMatrixType()==MatrixType::SPARSE)
                NOT_IMPLEMENTED;
            if ( b.GetMatrixType()==MatrixType::SPARSE)
            {
                if (c.GetMatrixType() == MatrixType::DENSE)
                {
                    CPUSparseMatrix<ElemType>::MultiplyAndWeightedAdd(alpha, *a.m_CPUMatrix, transposeA, *b.m_CPUSparseMatrix, transposeB, beta, *c.m_CPUMatrix);
                    c.SetDataLocation(CPU, DENSE);
                }
                else if (c.GetMatrixType() == MatrixType::SPARSE)
                {
                        CPUSparseMatrix<ElemType>::MultiplyAndAdd(alpha, *a.m_CPUMatrix, transposeA, *b.m_CPUSparseMatrix, transposeB, *c.m_CPUSparseMatrix);
                        c.SetDataLocation(CPU, SPARSE); 
                }
                else
                    NOT_IMPLEMENTED;
            }
            else
            {
                c.SwitchToMatrixType(MatrixType::DENSE);
                CPUMatrix<ElemType>::MultiplyAndWeightedAdd(alpha,*a.m_CPUMatrix,transposeA,*b.m_CPUMatrix,transposeB,beta,*c.m_CPUMatrix);
                c.SetDataLocation(CPU, DENSE);
            }

        }
        else //GPU operations
        {     
            if (a.m_matrixType==b.m_matrixType && b.m_matrixType==c.m_matrixType && a.m_matrixType==MatrixType::DENSE) //All dense
            {
                GPUMatrix<ElemType>::MultiplyAndWeightedAdd(alpha,*a.m_GPUMatrix,transposeA,*b.m_GPUMatrix,transposeB,beta,*c.m_GPUMatrix);
                c.SetDataLocation(GPU, DENSE);
            }
            else if (a.m_matrixType==MatrixType::SPARSE && b.m_matrixType==c.m_matrixType && b.m_matrixType==MatrixType::DENSE) //Sparse*Dense+Dense
            {
                GPUMatrix<ElemType> second = transposeB ? b.m_GPUMatrix->Transpose() : *b.m_GPUMatrix;
                GPUSparseMatrix<ElemType>::MultiplyAndWeightedAdd(alpha,*a.m_GPUSparseMatrix,transposeA,second,beta,*c.m_GPUMatrix);    
                c.SetDataLocation(GPU, DENSE);
            }
            else if (a.m_matrixType==MatrixType::DENSE && b.m_matrixType==MatrixType::SPARSE && c.m_matrixType==MatrixType::DENSE) //Dense*Sparse + Dense
            {
                if(b.m_GPUSparseMatrix->m_legacy == false) 
                {
                    // new GPU sparse matrix code
                    GPUSparseMatrix<ElemType>::MultiplyAndWeightedAdd(alpha, *a.m_GPUMatrix, transposeA, *b.m_GPUSparseMatrix, transposeB, beta, *c.m_GPUMatrix);
                }
                else 
                {
                    GPUMatrix<ElemType> firstDummy = transposeA ? a.m_GPUMatrix->Transpose()*alpha : (*a.m_GPUMatrix)*alpha;
                    GPUMatrix<ElemType> & first= firstDummy;				// GCC does not support mixing refs and non-refs
                    GPUSparseMatrix<ElemType> secondDummy = transposeB ? b.m_GPUSparseMatrix->Transpose() : *b.m_GPUSparseMatrix;
                    GPUSparseMatrix<ElemType> & second = secondDummy;
                    if (beta==0)
                    {
                        GPUSparseMatrix<ElemType>::Multiply(first,second,*c.m_GPUMatrix);
                    }
                    else
                    {   
                        Matrix<ElemType> tmp(c.GetNumRows(),c.GetNumCols(),(short)c.GetDeviceId());
                        GPUSparseMatrix<ElemType>::Multiply(first,second,*tmp.m_GPUMatrix);
                        c=tmp+c*beta;                               
                    }
                }
                c.SetDataLocation(GPU, DENSE);
            }
            else if (a.m_matrixType==MatrixType::DENSE && b.m_matrixType==MatrixType::SPARSE && c.m_matrixType==MatrixType::SPARSE) // h -> u0
            {
                // new GPU sparse matrix code
                GPUSparseMatrix<ElemType>::MultiplyAndAdd(alpha, *a.m_GPUMatrix, transposeA, *b.m_GPUSparseMatrix, transposeB, *c.m_GPUSparseMatrix);
                c.SetDataLocation(GPU, SPARSE); 
            }
            else if (a.m_matrixType==b.m_matrixType && b.m_matrixType==c.m_matrixType && a.m_matrixType==MatrixType::SPARSE)
            {
                GPUSparseMatrix<ElemType> firstDummy = alpha==1 ? *a.m_GPUSparseMatrix : (*a.m_GPUSparseMatrix)*alpha;
                GPUSparseMatrix<ElemType> & first = firstDummy;				 // By Malcolm.. gcc doesn't support auto
                if (beta==0)
                {
                    GPUSparseMatrix<ElemType>::Multiply(first,transposeA,*b.m_GPUSparseMatrix,transposeB,*c.m_GPUSparseMatrix);   
                    c.SetDataLocation(GPU, SPARSE); 
                }
                else
                {
                    GPUSparseMatrix<ElemType> tmp;                    
                    GPUSparseMatrix<ElemType>::Multiply(first,transposeA,*b.m_GPUSparseMatrix,transposeB,tmp);                
                    *c.m_GPUSparseMatrix = tmp + (*c.m_GPUSparseMatrix)*beta;  
                    c.SetDataLocation(GPU, SPARSE); 
                }
            }
            else
                NOT_IMPLEMENTED;
        }
    }

    /// <summary>Matrix-matrix multiply with col-major matrices (a and b may be transposed): c =  op(a) * op(b) + c</summary>
    /// <param name="a">Input matrix</param>
    /// <param name="transposeA">Whether matrix a is transposed</param>
    /// <param name="b">Input matrix</param>
    /// <param name="transposeB">Whether matrix b is transposed</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>
    void Matrix<ElemType>::MultiplyAndAdd(const Matrix<ElemType>& a, const bool transposeA, const Matrix<ElemType>& b, const bool transposeB, 
        Matrix<ElemType>& c)
    {
        return Matrix<ElemType>::MultiplyAndWeightedAdd(1.0, a, transposeA, b, transposeB, 1.0, c);
    }

    /// <summary>Matrix-matrix multiply with col-major matrices (a and b may be transposed): c =  op(a) * op(b)</summary>
    /// <param name="a">Input matrix</param>
    /// <param name="transposeA">Whether matrix a is transposed</param>
    /// <param name="b">Input matrix</param>
    /// <param name="transposeB">Whether matrix b is transposed</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>
    void Matrix<ElemType>::Multiply(const Matrix<ElemType>& a, const bool transposeA, const Matrix<ElemType>& b, const bool transposeB, 
        Matrix<ElemType>& c)
    {
        return Matrix<ElemType>::MultiplyAndWeightedAdd(1.0, a, transposeA, b, transposeB, 0.0, c);
    }

    /// <summary>Matrix-matrix multiply with col-major matrices (a and b are not transposed): c =  a * b</summary>
    /// <param name="a">Input matrix</param>
    /// <param name="b">Input matrix</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>
    void Matrix<ElemType>::Multiply(const Matrix<ElemType>& a, const Matrix<ElemType>& b, Matrix<ElemType>& c)
    {
        return Matrix<ElemType>::MultiplyAndWeightedAdd(1.0, a, false, b, false, 0.0, c);
    }


    /// <summary>Matrix-scalar multiply with col-major matrices: c = alpha * a + c</summary>
    /// if a is a column vector, add to all columns of c 
    /// if a is a row vector, add to all rows of c    
    /// <param name="alpha">Scalar</param>
    /// <param name="a">Input matrix</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>
    void Matrix<ElemType>::ScaleAndAdd(ElemType alpha, const Matrix<ElemType>& a, Matrix<ElemType>& c)
    {
        if (a.IsEmpty() || c.IsEmpty())
            throw std::logic_error("ScaleAndAdd:  one of the input matrices is empty."); 

        DecideAndMoveToRightDevice(c, a);        

        if (a.GetMatrixType() == c.GetMatrixType())
            {
            DISPATCH_MATRIX_ON_FLAG(&c,
                &c,
                CPUMatrix<ElemType>::ScaleAndAdd(alpha,*a.m_CPUMatrix,*c.m_CPUMatrix), 
                GPUMatrix<ElemType>::ScaleAndAdd(alpha,*a.m_GPUMatrix,*c.m_GPUMatrix), 
                NOT_IMPLEMENTED, 
                GPUSparseMatrix<ElemType> b = move(*c.m_GPUSparseMatrix); GPUSparseMatrix<ElemType>::ScaleAndAdd(alpha,*a.m_GPUSparseMatrix,1,b,*c.m_GPUSparseMatrix)
                );                
        }
        else
        {
            DISPATCH_MATRIX_ON_FLAG(&c,
                nullptr,
                CPUSparseMatrix<ElemType>::ScaleAndAdd(alpha,*a.m_CPUSparseMatrix,*c.m_CPUMatrix); c.SetDataLocation(CPU),
                if(a.m_GPUSparseMatrix->m_legacy) {
                    GPUSparseMatrix<ElemType>::ScaleAndAdd(alpha,*a.m_GPUSparseMatrix,1,*c.m_GPUMatrix,*c.m_GPUMatrix); 
                }
                else // new GPU sparse matrix code 
                {
                    GPUSparseMatrix<ElemType>::ScaleAndAdd(alpha,*a.m_GPUSparseMatrix,*c.m_GPUMatrix); 
                }
                c.SetDataLocation(GPU), 
                NOT_IMPLEMENTED, 
                c.m_GPUMatrix = new GPUMatrix<ElemType>(c.m_GPUSparseMatrix->CopyToDenseMatrix());
                GPUSparseMatrix<ElemType>::ScaleAndAdd(alpha,*a.m_GPUMatrix,1,*c.m_GPUSparseMatrix,*c.m_GPUMatrix);                
                    delete c.m_GPUSparseMatrix; c.m_GPUSparseMatrix = NULL;
                    c.SetDataLocation(GPU, DENSE)
                );                
        }       
    }

    /// <summary>Matrix-scalar multiply with col-major matrices: c = alpha * a + beta * c</summary>
    /// if a is a column vector, add to all columns of c 
    /// if a is a row vector, add to all rows of c    
    /// <param name="alpha">Scalar</param>
    /// <param name="a">Input matrix</param>
    /// <param name="beta">Scalar</param>
    /// <param name="c">Resulting matrix, caller is responsible for allocating this</param>
    template<class ElemType>
    void Matrix<ElemType>::ScaleAndAdd(ElemType alpha, const Matrix<ElemType>& a, ElemType beta, Matrix<ElemType>& c)
    {
        if (beta==1)
            ScaleAndAdd(alpha,a,c);
        else if (beta==0)
        {
            Scale(alpha,a,c);
        }
        else
        {
            ScaleAndAdd(alpha/beta,a,c); // c1=alpha/beta * a + c
            Scale(beta,c); // c/beta * beta
        }
    }

    /// <summary>c += alpha * (a-b)</summary>
    /// if a, b, c  must have same dim 
    /// <param name="alpha">Scalar</param>
    /// <param name="a">Input matrix</param>
    /// <param name="b">Input matrix</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>
    void Matrix<ElemType>::AddScaledDifference(const ElemType alpha, const Matrix<ElemType>& a, const Matrix<ElemType>& b, Matrix<ElemType>& c)
    {
        DecideAndMoveToRightDevice(c, a, b);        
        if (!(a.GetMatrixType() == b.GetMatrixType() && a.GetMatrixType() == c.GetMatrixType() ))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(&c,
            &c,
            CPUMatrix<ElemType>::AddScaledDifference(alpha,*a.m_CPUMatrix,*b.m_CPUMatrix, *c.m_CPUMatrix), 
            GPUMatrix<ElemType>::AddScaledDifference(alpha,*a.m_GPUMatrix,*b.m_GPUMatrix,*c.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

    }

    /// <summary> c = alpha * (a-b)</summary>
    /// if a, b, c  must have same dim 
    /// <param name="alpha">Scalar</param>
    /// <param name="a">Input matrix</param>
    /// <param name="b">Input matrix</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>    
    void Matrix<ElemType>::AssignScaledDifference(const ElemType alpha, const Matrix<ElemType>& a, const Matrix<ElemType>& b, Matrix<ElemType>& c)
    {
        DecideAndMoveToRightDevice(a, b, c);        

        if (!(a.GetMatrixType() == b.GetMatrixType()))
            NOT_IMPLEMENTED;

        c.SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&c,
            &c,
            CPUMatrix<ElemType>::AssignScaledDifference(alpha,*a.m_CPUMatrix,*b.m_CPUMatrix, *c.m_CPUMatrix), 
            GPUMatrix<ElemType>::AssignScaledDifference(alpha,*a.m_GPUMatrix,*b.m_GPUMatrix,*c.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

    }

    /// <summary>c += alpha * (a-b)</summary>
    /// if a, b, c  must have same dim 
    /// <param name="alpha">Scalar</param>
    /// <param name="a">Input matrix</param>
    /// <param name="b">Input matrix</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>
    void Matrix<ElemType>::AddScaledDifference(const Matrix<ElemType>& alpha, const Matrix<ElemType>& a, const Matrix<ElemType>& b, Matrix<ElemType>& c)
    {
        DecideAndMoveToRightDevice(c, a, b);        
        alpha._transferToDevice(c.GetDeviceId());

        if (!(a.GetMatrixType() == b.GetMatrixType() && a.GetMatrixType() == c.GetMatrixType() && a.GetMatrixType() == alpha.GetMatrixType()))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(&c,
            &c,
            CPUMatrix<ElemType>::AddScaledDifference(*alpha.m_CPUMatrix,*a.m_CPUMatrix,*b.m_CPUMatrix, *c.m_CPUMatrix), 
            GPUMatrix<ElemType>::AddScaledDifference(*alpha.m_GPUMatrix,*a.m_GPUMatrix,*b.m_GPUMatrix,*c.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
    }

    /// <summary> c = alpha * (a-b)</summary>
    /// if a, b, c  must have same dim 
    /// <param name="alpha">Scalar</param>
    /// <param name="a">Input matrix</param>
    /// <param name="b">Input matrix</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>    
    void Matrix<ElemType>::AssignScaledDifference(const Matrix<ElemType>& alpha, const Matrix<ElemType>& a, const Matrix<ElemType>& b, Matrix<ElemType>& c)
    {
        DecideAndMoveToRightDevice(a, b, alpha);        
        c._transferToDevice(a.GetDeviceId());
        
        if (!(a.GetMatrixType() == b.GetMatrixType() && a.GetMatrixType() == alpha.GetMatrixType()))
            NOT_IMPLEMENTED;

        c.SwitchToMatrixType(a.GetMatrixType());       

        DISPATCH_MATRIX_ON_FLAG(&c,
            nullptr,
            CPUMatrix<ElemType>::AssignScaledDifference(*alpha.m_CPUMatrix,*a.m_CPUMatrix,*b.m_CPUMatrix, *c.m_CPUMatrix), 
            GPUMatrix<ElemType>::AssignScaledDifference(*alpha.m_GPUMatrix,*a.m_GPUMatrix,*b.m_GPUMatrix,*c.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );

    }

    //c[ci,cj] += a[ai,aj]
    template<class ElemType>
    void Matrix<ElemType>::AddElementToElement(const Matrix<ElemType>& a, const size_t ai, const size_t aj, Matrix<ElemType>& c, const size_t ci, const size_t cj)
    {
        DecideAndMoveToRightDevice(c, a);        

        if (c.GetMatrixType() != a.GetMatrixType())
                NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(&c,
            &c,
            CPUMatrix<ElemType>::AddElementToElement(*a.m_CPUMatrix, ai, aj, *c.m_CPUMatrix, ci, cj), 
            GPUMatrix<ElemType>::AddElementToElement(*a.m_GPUMatrix, ai, aj, *c.m_GPUMatrix, ci, cj), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );                
    }

    //c[ci,cj] = a[ai,aj]
    template<class ElemType>
    void Matrix<ElemType>::AssignElementToElement(const Matrix<ElemType>& a, const size_t ai, const size_t aj, Matrix<ElemType>& c, const size_t ci, const size_t cj)
    {
        DecideAndMoveToRightDevice(c, a);        

        if (c.GetMatrixType() != a.GetMatrixType())
                NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(&c,
            &c,
            CPUMatrix<ElemType>::AssignElementToElement(*a.m_CPUMatrix, ai, aj, *c.m_CPUMatrix, ci, cj), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
    }

    /// <summary>Matrix-scalar multiply with col-major matrices: c = alpha * a</summary>
    /// <param name="alpha">Scalar</param>
    /// <param name="a">Input matrix</param>
    /// <param name="c">Resulting matrix, user is responsible for allocating this</param>
    template<class ElemType>
    void Matrix<ElemType>::Scale(ElemType alpha, const Matrix<ElemType>& a, Matrix<ElemType>& c)
    {
        DecideAndMoveToRightDevice(c, a);      
        c.SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&c,
            &c,
            CPUMatrix<ElemType>::Scale(alpha,*a.m_CPUMatrix,*c.m_CPUMatrix), 
            GPUMatrix<ElemType>::Scale(alpha,*a.m_GPUMatrix,*c.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            *c.m_GPUSparseMatrix = (*a.m_GPUSparseMatrix)*alpha
            );
                
    }

    /// <summary>Matrix-scalar multiply with col-major matrices: a = alpha * a</summary>
    /// <param name="alpha">Scalar</param>
    /// <param name="a">Input matrix</param>
    template<class ElemType>
    void Matrix<ElemType>::Scale(ElemType alpha, Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("Scale:  Input matrix a is empty.");

        DISPATCH_MATRIX_ON_FLAG(&a,
            &a,
            CPUMatrix<ElemType>::Scale(alpha,*a.m_CPUMatrix), 
            GPUMatrix<ElemType>::Scale(alpha,*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            GPUSparseMatrix<ElemType>::Scale(alpha,*a.m_GPUSparseMatrix)
            );
                
    }

    /// <summary>Matrix scalar matrix multiply with col-major matrices: a = alpha[0,0] * a</summary>
    /// <param name="alpha">1x1 matrix</param>
    /// <param name="a">Input matrix</param>
    template<class ElemType>
    void Matrix<ElemType>::Scale(Matrix<ElemType>& alpha, Matrix<ElemType>& a)
    {
        if (a.IsEmpty())
            throw std::logic_error("Scale:  Input matrix a is empty.");

        DecideAndMoveToRightDevice(a,alpha);

        if (a.GetMatrixType() != alpha.GetMatrixType())
            NOT_IMPLEMENTED;            

        DISPATCH_MATRIX_ON_FLAG(&a,
            nullptr,
            CPUMatrix<ElemType>::Scale(*alpha.m_CPUMatrix,*a.m_CPUMatrix), 
            GPUMatrix<ElemType>::Scale(*alpha.m_GPUMatrix,*a.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
    }

    template<class ElemType>
    void Matrix<ElemType>::InnerProduct (const Matrix<ElemType>& a, const Matrix<ElemType>& b, Matrix<ElemType>& c, const bool isColWise)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("InnerProduct:  one of the input matrix is empty.");

        DecideAndMoveToRightDevice(a, b, c);

        if (a.GetMatrixType() != b.GetMatrixType())
            NOT_IMPLEMENTED;

        c.SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&c,
            &c,
            CPUMatrix<ElemType>::InnerProduct(*a.m_CPUMatrix,*b.m_CPUMatrix,*c.m_CPUMatrix,isColWise), 
            GPUMatrix<ElemType>::InnerProduct(*a.m_GPUMatrix,*b.m_GPUMatrix,*c.m_GPUMatrix,isColWise), 
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED
            );
                
    }

    template<class ElemType>
    ElemType Matrix<ElemType>::InnerProductOfMatrices(const Matrix<ElemType>& a, const Matrix<ElemType>& b)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("InnerProductOfMatrices:  one of the input matrices is empty.");

        DecideAndMoveToRightDevice(a,b);        

        if (a.GetMatrixType() == b.GetMatrixType())
            {
            DISPATCH_MATRIX_ON_FLAG(&a,
                nullptr,
                return CPUMatrix<ElemType>::InnerProductOfMatrices(*a.m_CPUMatrix,*b.m_CPUMatrix), 
                return GPUMatrix<ElemType>::InnerProductOfMatrices(*a.m_GPUMatrix,*b.m_GPUMatrix), 
                NOT_IMPLEMENTED, 
                NOT_IMPLEMENTED
                );                
            }
            else
            {
            DISPATCH_MATRIX_ON_FLAG(&a,
                nullptr,
                NOT_IMPLEMENTED, 
                return GPUSparseMatrix<ElemType>::InnerProductOfMatrices(*a.m_GPUMatrix,*b.m_GPUSparseMatrix), 
                NOT_IMPLEMENTED, 
                return GPUSparseMatrix<ElemType>::InnerProductOfMatrices(*a.m_GPUSparseMatrix,*b.m_GPUMatrix)
                );                
        }
    }

    template<class ElemType>
    Matrix<ElemType>& Matrix<ElemType>::AssignInnerProductOfMatrices(const Matrix<ElemType>& a, const Matrix<ElemType>& b)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("InnerProductOfMatrices:  one of the input matrices is empty.");

        this->Resize(1,1);       

        DecideAndMoveToRightDevice(a, b, *this);        
    
        if (a.GetMatrixType() == b.GetMatrixType())
            {
            SwitchToMatrixType(a.GetMatrixType());

            DISPATCH_MATRIX_ON_FLAG(&a,
                this,
                this->m_CPUMatrix->SetValue(CPUMatrix<ElemType>::InnerProductOfMatrices(*a.m_CPUMatrix,*b.m_CPUMatrix)), 
                this->m_GPUMatrix->AssignInnerProductOfMatrices(*a.m_GPUMatrix,*b.m_GPUMatrix), 
                NOT_IMPLEMENTED, 
                NOT_IMPLEMENTED
                );                
            }
            else
            {
                NOT_IMPLEMENTED;                
            }
        
        return *this;
    }

    template<class ElemType>
    void Matrix<ElemType>::ElementWisePower (ElemType alpha, const Matrix<ElemType>& a, Matrix<ElemType>& c)
    {
        if (a.IsEmpty())
            throw std::logic_error("Scale:  The input matrix a is empty.");

        DecideAndMoveToRightDevice(a, c);        
        c.SwitchToMatrixType(a.GetMatrixType());

        DISPATCH_MATRIX_ON_FLAG(&c,
            nullptr,
            CPUMatrix<ElemType>::ElementWisePower(alpha,*a.m_CPUMatrix,*c.m_CPUMatrix), 
            GPUMatrix<ElemType>::ElementWisePower(alpha,*a.m_GPUMatrix,*c.m_GPUMatrix), 
            NOT_IMPLEMENTED, 
            GPUSparseMatrix<ElemType>::ElementWisePower(alpha,*a.m_GPUSparseMatrix,*c.m_GPUSparseMatrix)
            );                
    }

    template<class ElemType>
    bool Matrix<ElemType>::AreEqual(const Matrix<ElemType>& a, const Matrix<ElemType>& b, const ElemType threshold /*= 1e-8*/)
    {
        if (a.IsEmpty() || b.IsEmpty())
            throw std::logic_error("AreEqual: one of the input matrices is empty.");

        if (a.GetNumRows()  != b.GetNumRows() || a.GetNumCols() != b.GetNumCols())
            return false;

        DecideAndMoveToRightDevice(a,b);        

        if (a.GetMatrixType() == b.GetMatrixType())
            {
            DISPATCH_MATRIX_ON_FLAG(&a,
                nullptr,
                return CPUMatrix<ElemType>::AreEqual(*a.m_CPUMatrix, *b.m_CPUMatrix, threshold),
                return GPUMatrix<ElemType>::AreEqual(*a.m_GPUMatrix, *b.m_GPUMatrix, threshold),
                NOT_IMPLEMENTED; return false ,
                return GPUSparseMatrix<ElemType>::AreEqual(*a.m_GPUSparseMatrix, *b.m_GPUSparseMatrix, threshold)
                );                
            }
            else
            {
            DISPATCH_MATRIX_ON_FLAG(&a,
                nullptr,
                NOT_IMPLEMENTED; return false,
                return GPUSparseMatrix<ElemType>::AreEqual(*a.m_GPUMatrix, *b.m_GPUSparseMatrix, threshold),
                NOT_IMPLEMENTED; return false,
                return GPUSparseMatrix<ElemType>::AreEqual(*a.m_GPUSparseMatrix, *b.m_GPUMatrix, threshold)
                );                
        }
        
    }

    // diagnostics helper to check if matrix has a NaN
    // This is very slow.
    template<class ElemType>
    bool Matrix<ElemType>::HasNan (const char * name) const
    {
        // const auto & us = *this;
        const Matrix<ElemType> & us = *this;

        foreach_coord (i, j, us)
            if (std::isnan(us(i, j)))
            {
                fprintf (stderr, "hasnan: NaN detected at %s (%ld,%ld)\n", name, i, j);
                return true;
            }
            return false;
    }
#define CheckNan(m) m.HasNan (#m)

    // another diagnostics helper to check if matrix has a NaN
    // This is used at load and save time. This test is slow.

    template<class ElemType>
    size_t Matrix<ElemType>::CountNanInf() const
    {
        const auto & us = *this;
        size_t n = 0;   // number of NaNs/INF found
        foreach_coord (i, j, us)
        {
            auto val = us(i,j);
            if (std::isnan (val) || !std::isfinite (val))
                n++;
        }
        return n;
    }

    template<class ElemType>
    short Matrix<ElemType>::GetBestGPUDeviceId()
    { 
        return (short)GPUMatrix<ElemType>::GetBestGPUDeviceId();
    }

    template<class ElemType>
    ElemType Matrix<ElemType>::Exp10(ElemType num)
    {
        return (ElemType)exp(num*2.302585093);
    }

    template<class ElemType>
    ElemType Matrix<ElemType>::Mod(ElemType x, ElemType y)
    {
        assert(y > 0);
        if (y <= 0)
            throw std::logic_error("y is smaller than zero");

        ElemType r = x - y * floor(x / y);
        return r; 
    }

	template<class ElemType>
	ElemType Matrix<ElemType>::LogAdd(ElemType x, ElemType y)
	{
		ElemType temp, diff, z;

		if (x < y) {
			temp = x; x = y; y = temp;
		}
		diff = y - x;
		if (diff < MINLOGEXP)
		{
			return (ElemType) ((x < LSMALL) ? LZERO : x);
		}
		else
		{
			z = exp(diff);
                        return (ElemType) (x + log(1.0 + z));
		}
	}

	template<class ElemType>
    void Matrix<ElemType>::ClassEntropy(const Matrix<ElemType>& a, const Matrix<ElemType>& wgt,
        const Matrix<ElemType> & label, const Matrix<ElemType>* cls, 
        const Matrix<ElemType>* idx2cls,  Matrix<ElemType>& etp, Matrix<ElemType>& entropyScore)
    {
        DecideAndMoveToRightDevice(a,label,entropyScore); 
        wgt._transferToDevice(a.GetDeviceId());
        cls->_transferToDevice(a.GetDeviceId());
        idx2cls->_transferToDevice(a.GetDeviceId());
        etp._transferToDevice(a.GetDeviceId());

        if (!(a.GetMatrixType() == entropyScore.GetMatrixType() && a.GetMatrixType() == wgt.GetMatrixType()
              && a.GetMatrixType() == cls->GetMatrixType() && a.GetMatrixType() == idx2cls->GetMatrixType()
              && label.GetMatrixType() == etp.GetMatrixType()))
              NOT_IMPLEMENTED; 

        if (a.GetMatrixType() == label.GetMatrixType())
            {
            NOT_IMPLEMENTED;       
            }
            else
            {
            DISPATCH_MATRIX_ON_FLAG(&a,
                &entropyScore,
                CPUSparseMatrix<ElemType>::ClassEntropy(*a.m_CPUMatrix,*wgt.m_CPUMatrix,*label.m_CPUSparseMatrix,
                    *(cls->m_CPUMatrix), *(idx2cls->m_CPUMatrix), 
                    *etp.m_CPUSparseMatrix, *entropyScore.m_CPUMatrix), 
                GPUSparseMatrix<ElemType>::ClassEntropy(*a.m_GPUMatrix,*wgt.m_GPUMatrix,*label.m_GPUSparseMatrix,
                    *(cls->m_GPUMatrix), *(idx2cls->m_GPUMatrix), 
                     *etp.m_GPUSparseMatrix, *entropyScore.m_GPUMatrix), 
                NOT_IMPLEMENTED, 
                NOT_IMPLEMENTED
                );                
        }
        
    }

    template<class ElemType>
    void Matrix<ElemType>::ClassEntropyError(const Matrix<ElemType>& a)
    {
        DISPATCH_MATRIX_ON_FLAG(&a,
            &a,
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            CPUSparseMatrix<ElemType>::ClassEntropyError(*a.m_CPUSparseMatrix), 
            GPUSparseMatrix<ElemType>::ClassEntropyError(*a.m_GPUSparseMatrix)
            );                
    }

    template<class ElemType>
    void Matrix<ElemType>::ClassEntropyGradientOfInput(
        const Matrix<ElemType>& error,
        const Matrix<ElemType>& weight,
        Matrix<ElemType>& grd) 
    {
        DecideAndMoveToRightDevice(error,weight, grd); 

        if (weight.GetMatrixType() != DENSE || grd.GetMatrixType() != DENSE)
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(&error,
            nullptr,
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            CPUSparseMatrix<ElemType>::ClassEntropyGradientOfInput(*error.m_CPUSparseMatrix, *weight.m_CPUMatrix, *grd.m_CPUMatrix); grd.SetDataLocation(CPU), 
            GPUSparseMatrix<ElemType>::ClassEntropyGradientOfInput(*error.m_GPUSparseMatrix, *weight.m_GPUMatrix, *grd.m_GPUMatrix); grd.SetDataLocation(GPU)
            );               
    }

    template<class ElemType>
    void Matrix<ElemType>::ClassEntropyGradientOfWeight( 
        const Matrix<ElemType>& error, 
        const Matrix<ElemType>& input, 
        const Matrix<ElemType>& weight,
        const Matrix<ElemType> & label, 
        const Matrix<ElemType>* cls, 
        const Matrix<ElemType>* idx2cls, 
        Matrix<ElemType>& grd)
    {
        DecideAndMoveToRightDevice(error,input,weight);  
        label._transferToDevice(error.GetDeviceId());
        cls->_transferToDevice(error.GetDeviceId());
        idx2cls->_transferToDevice(error.GetDeviceId());
        grd._transferToDevice(error.GetDeviceId());

        if (!(error.GetMatrixType() == SPARSE && label.GetMatrixType() == SPARSE && grd.GetMatrixType() == SPARSE 
            && input.GetMatrixType() == DENSE && cls->GetMatrixType() == DENSE && idx2cls->GetMatrixType() == DENSE))
            NOT_IMPLEMENTED;

        DISPATCH_MATRIX_ON_FLAG(&error,
            &grd,
            NOT_IMPLEMENTED, 
            NOT_IMPLEMENTED, 
            CPUSparseMatrix<ElemType>::ClassEntropyGradientOfWeight(
                *error.m_CPUSparseMatrix,
                *input.m_CPUMatrix, 
                *label.m_CPUSparseMatrix, 
                *(cls->m_CPUMatrix), 
                *(idx2cls->m_CPUMatrix),
                *grd.m_CPUSparseMatrix), 
            GPUSparseMatrix<ElemType>::ClassEntropyGradientOfWeight(
                *error.m_GPUSparseMatrix,
                *input.m_GPUMatrix, 
                *label.m_GPUSparseMatrix,
                *(cls->m_GPUMatrix), 
                *(idx2cls->m_GPUMatrix),
                *grd.m_GPUSparseMatrix)
            );                
    }

#pragma endregion Static BLAS Functions

    template class Matrix<float>; 
    template class Matrix<double>;    
}}}
