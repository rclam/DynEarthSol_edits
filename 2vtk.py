#!/usr/bin/env python
# encoding: utf-8
'''Convert the binary output of DynEarthSol3D to VTK files.

usage: 2vtk.py [-a -c -m -t -h] modelname [start [end [delta]]]]

options:
    -a          save data in ASCII format (default: binary)
    -c          save files in current directory
    -m          save marker data
    -t          save all tensor components (default: no component)
    -h,--help   show this help
'''

from __future__ import print_function, unicode_literals
import sys, os
import base64, zlib
import numpy as np
from Dynearthsol import Dynearthsol

# Save in ASCII or encoded binary.
# Some old VTK programs cannot read binary VTK files.
output_in_binary = True

# Save the resultant vtu files in current directory?
output_in_cwd = False

# Save indivisual components?
output_tensor_components = False

# Save markers?
output_markers = False


def main(modelname, start, end, delta):
    prefix = modelname
    if output_in_cwd:
        output_prefix = os.path.basename(modelname)
    else:
        output_prefix = modelname

    des = Dynearthsol(modelname)

    if end == -1:
        end = len(des.frames)

    for i in range(start, end, delta):
        frame = des.frames[i]
        nnode = des.nnode_list[i]
        nelem = des.nelem_list[i]

        des.read_header(frame)
        suffix = '{0:0=6}'.format(frame)
        print('Converting frame #{0}'.format(suffix), end='\r', file=sys.stderr)

        filename = '{0}.{1}.vtu'.format(output_prefix, suffix)
        fvtu = open(filename, 'wb')

        try:
            vtu_header(fvtu, nnode, nelem)

            #
            # node-based field
            #
            fvtu.write(b'  <PointData>\n')

            # averaged velocity is more stable and is preferred
            try:
                convert_field(des, frame, 'velocity averaged', fvtu)
            except KeyError:
                convert_field(des, frame, 'velocity', fvtu)

            convert_field(des, frame, 'force', fvtu)

            convert_field(des, frame, 'temperature', fvtu)
            #convert_field(des, frame, 'bcflag', fvtu)
            #convert_field(des, frame, 'mass', fvtu)
            #convert_field(des, frame, 'tmass', fvtu)
            #convert_field(des, frame, 'volume_n', fvtu)

            # node number for debugging
            vtk_dataarray(fvtu, np.arange(nnode, dtype=np.int32), 'node number')

            fvtu.write(b'  </PointData>\n')
            #
            # element-based field
            #
            fvtu.write(b'  <CellData>\n')

            #convert_field(des, frame, 'volume', fvtu)
            #convert_field(des, frame, 'edvoldt', fvtu)

            convert_field(des, frame, 'mesh quality', fvtu)
            convert_field(des, frame, 'plastic strain', fvtu)
            convert_field(des, frame, 'plastic strain-rate', fvtu)

            strain_rate = des.read_field(frame, 'strain-rate')
            srII = second_invariant(strain_rate)
            vtk_dataarray(fvtu, np.log10(srII+1e-45), 'strain-rate II log10')
            if output_tensor_components:
                for d in range(des.nstr):
                    vtk_dataarray(fvtu, strain_rate[:,d], 'strain-rate ' + des.component_names[d])

            strain = des.read_field(frame, 'strain')
            sI = first_invariant(strain)
            sII = second_invariant(strain)
            vtk_dataarray(fvtu, sI, 'strain I')
            vtk_dataarray(fvtu, sII, 'strain II')
            if output_tensor_components:
                for d in range(des.nstr):
                    vtk_dataarray(fvtu, strain[:,d], 'strain ' + des.component_names[d])

            # averaged stress is more stable and is preferred
            try:
                stress = des.read_field(frame, 'stress averaged')
            except KeyError:
                stress = des.read_field(frame, 'stress')
            tI = first_invariant(stress)
            tII = second_invariant(stress)
            vtk_dataarray(fvtu, tI, 'stress I')
            vtk_dataarray(fvtu, tII, 'stress II')
            if output_tensor_components:
                for d in range(des.ndims):
                    vtk_dataarray(fvtu, stress[:,d] - tI, 'stress ' + des.component_names[d] + ' dev.')
                for d in range(des.ndims, des.nstr):
                    vtk_dataarray(fvtu, stress[:,d], 'stress ' + des.component_names[d])

            convert_field(des, frame, 'density', fvtu)
            convert_field(des, frame, 'material', fvtu)
            convert_field(des, frame, 'viscosity', fvtu)
            effvisc = tII / (srII + 1e-45)
            vtk_dataarray(fvtu, effvisc, 'effective viscosity')

            # element number for debugging
            vtk_dataarray(fvtu, np.arange(nelem, dtype=np.int32), 'elem number')

            fvtu.write(b'  </CellData>\n')

            #
            # node coordinate
            #
            fvtu.write(b'  <Points>\n')
            convert_field(des, frame, 'coordinate', fvtu)
            fvtu.write(b'  </Points>\n')

            #
            # element connectivity & types
            #
            fvtu.write(b'  <Cells>\n')
            convert_field(des, frame, 'connectivity', fvtu)
            vtk_dataarray(fvtu, (des.ndims+1)*np.array(range(1, nelem+1), dtype=np.int32), 'offsets')
            if des.ndims == 2:
                # VTK_ TRIANGLE == 5
                celltype = 5
            else:
                # VTK_ TETRA == 10
                celltype = 10
            vtk_dataarray(fvtu, celltype*np.ones((nelem,), dtype=np.int32), 'types')
            fvtu.write(b'  </Cells>\n')

            vtu_footer(fvtu)
            fvtu.close()

        except:
            # delete partial vtu file
            fvtu.close()
            os.remove(filename)
            raise

        #
        # Converting marker
        #
        if output_markers:
            # ordinary markerset
            filename = '{0}.{1}.vtp'.format(output_prefix, suffix)
            output_vtp_file(des, frame, filename, 'markerset')

            # hydrous markerset
            if 'hydrous-markerset size' in des.field_pos:
                filename = '{0}.hyd-ms.{1}.vtp'.format(output_prefix, suffix)
                output_vtp_file(des, frame, filename, 'hydrous-markerset')

    print()
    return


def output_vtp_file(des, frame, filename, markersetname):
    fvtp = open(filename, 'wb')

    class MarkerSizeError(RuntimeError):
        pass

    try:
        # read data
        marker_data = des.read_markers(frame, markersetname)
        nmarkers = marker_data['size']

        if nmarkers <= 0:
            raise MarkerSizeError()

        # write vtp header
        vtp_header(fvtp, nmarkers)

        # point-based data
        fvtp.write('  <PointData>\n')
        name = markersetname + '.mattype'
        vtk_dataarray(fvtp, marker_data[name], name)
        name = markersetname + '.elem'
        vtk_dataarray(fvtp, marker_data[name], name)
        name = markersetname + '.id'
        vtk_dataarray(fvtp, marker_data[name], name)
        fvtp.write('  </PointData>\n')

        # point coordinates
        fvtp.write('  <Points>\n')
        field = marker_data[markersetname + '.coord']
        if des.ndims == 2:
            # VTK requires vector field (velocity, coordinate) has 3 components.
            # Allocating a 3-vector tmp array for VTK data output.
            tmp = np.zeros((nmarkers, 3), dtype=field.dtype)
            tmp[:,:des.ndims] = field
        else:
            tmp = field

        vtk_dataarray(fvtp, tmp, markersetname + '.coord', 3)
        fvtp.write('  </Points>\n')

        vtp_footer(fvtp)
        fvtp.close()

    except MarkerSizeError:
        # delete partial vtp file
        fvtp.close()
        os.remove(filename)
        # skip this frame

    except:
        # delete partial vtp file
        fvtp.close()
        os.remove(filename)
        raise

    return


def convert_field(des, frame, name, fvtu):
    field = des.read_field(frame, name)
    if name in ('coordinate', 'velocity', 'velocity averaged', 'force'):
        if des.ndims == 2:
            # VTK requires vector field (velocity, coordinate) has 3 components.
            # Allocating a 3-vector tmp array for VTK data output.
            i = des.frames.index(frame)
            tmp = np.zeros((des.nnode_list[i], 3), dtype=field.dtype)
            tmp[:,:des.ndims] = field
        else:
            tmp = field

        # Rename 'velocity averaged' to 'velocity'
        if name == 'velocity averaged': name = 'velocity'

        vtk_dataarray(fvtu, tmp, name, 3)
    else:
        vtk_dataarray(fvtu, field, name)
    return


def vtk_dataarray(f, data, data_name=None, data_comps=None):
    if data.dtype in (np.int32,):
        dtype = 'Int32'
    elif data.dtype in (np.single, np.float32):
        dtype = 'Float32'
    elif data.dtype in (np.double, np.float64):
        dtype = 'Float64'
    else:
        raise Error('Unknown data type: ' + name)

    name = ''
    if data_name:
        name = 'Name="{0}"'.format(data_name)

    ncomp = ''
    if data_comps:
        ncomp = 'NumberOfComponents="{0}"'.format(data_comps)

    if output_in_binary:
        fmt = 'binary'
    else:
        fmt = 'ascii'
    header = '<DataArray type="{0}" {1} {2} format="{3}">\n'.format(
        dtype, name, ncomp, fmt)
    f.write(header.encode('ascii'))
    if output_in_binary:
        header = np.zeros(4, dtype=np.int32)
        header[0] = 1
        a = data.tostring()
        header[1] = len(a)
        header[2] = len(a)
        b = zlib.compress(a)
        header[3] = len(b)
        f.write(base64.standard_b64encode(header.tostring()))
        f.write(base64.standard_b64encode(b))
    else:
        data.tofile(f, sep=b' ')
    f.write(b'\n</DataArray>\n')
    return


def vtu_header(f, nnode, nelem):
    f.write(
'''<?xml version="1.0"?>
<VTKFile type="UnstructuredGrid" version="0.1" byte_order="LittleEndian" compressor="vtkZLibDataCompressor">
<UnstructuredGrid>
<Piece NumberOfPoints="{0}" NumberOfCells="{1}">
'''.format(nnode, nelem).encode('ascii'))
    return


def vtu_footer(f):
    f.write(
b'''</Piece>
</UnstructuredGrid>
</VTKFile>
''')
    return


def vtp_header(f, nmarkers):
    f.write(
'''<?xml version="1.0"?>
<VTKFile type="PolyData" version="0.1" byte_order="LittleEndian" compressor="vtkZLibDataCompressor">
<PolyData>
<Piece NumberOfPoints="{0}">
'''.format(nmarkers).encode('ascii'))
    return


def vtp_footer(f):
    f.write(
b'''</Piece>
</PolyData>
</VTKFile>
''')
    return


def first_invariant(t):
    nstr = t.shape[1]
    ndims = 2 if (nstr == 3) else 3
    return np.sum(t[:,:ndims], axis=1) / ndims


def second_invariant(t):
    '''The second invariant of the deviatoric part of a symmetric tensor t,
    where t[:,0:ndims] are the diagonal components;
      and t[:,ndims:] are the off-diagonal components.'''
    nstr = t.shape[1]

    # second invariant: sqrt(0.5 * t_ij**2)
    if nstr == 3:  # 2D
        return np.sqrt(0.25 * (t[:,0] - t[:,1])**2 + t[:,2]**2)
    else:  # 3D
        a = (t[:,0] + t[:,1] + t[:,2]) / 3
        return np.sqrt( 0.5 * ((t[:,0] - a)**2 + (t[:,1] - a)**2 + (t[:,2] - a)**2) +
                        t[:,3]**2 + t[:,4]**2 + t[:,5]**2)


if __name__ == '__main__':

    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    else:
        for arg in sys.argv[1:]:
            if arg.lower() in ('-h', '--help'):
                print(__doc__)
                sys.exit(0)

    if '-a' in sys.argv:
        output_in_binary = False
    if '-c' in sys.argv:
        output_in_cwd = True
    if '-t' in sys.argv:
        output_tensor_components = True
    if '-m' in sys.argv:
        output_markers = True

    # delete options
    for i in range(len(sys.argv)):
        if sys.argv[1][0] == '-':
            del sys.argv[1]
        else:
            # the rest of argv cannot be options
            break

    modelname = sys.argv[1]

    if len(sys.argv) < 3:
        start = 0
    else:
        start = int(sys.argv[2])

    if len(sys.argv) < 4 or int(sys.argv[3]) == -1:
        end = -1
    else:
        end = int(sys.argv[3]) + 1

    if len(sys.argv) < 5:
        delta = 1
    else:
        delta = int(sys.argv[4])

    main(modelname, start, end, delta)
