// Copyright 2017 Global Phasing Ltd.

#include <cstdio>   // for printf, fprintf
#include <cstdlib>  // for atof
#include "gemmi/ccp4.hpp"
#include "gemmi/solmask.hpp"   // for SolventMasker
#include "gemmi/symmetry.hpp"
#include "gemmi/mmread_gz.hpp" // for read_structure_gz
#include "timer.h"

#define GEMMI_PROG mask
#include "options.h"

namespace {

enum OptionIndex {
  Timing=4, GridSpac, GridDims, Radius, RProbe, RShrink,
  IslandLimit, Hydrogens, AnyOccupancy, CctbxCompat, RefmacCompat, Invert,
  SetOccupancy, Verbosity
};

struct MaskArg {
  static option::ArgStatus FileFormat(const option::Option& option, bool msg) {
    return Arg::Choice(option, msg, {"coor", "map", "none"});
  }
};

const option::Descriptor Usage[] = {
  { NoOp, 0, "", "", Arg::None,
    "Usage:\n " EXE_NAME " [options] INPUT output.msk"
    "\n\nMakes a bulk-solvent mask in the CCP4 format."
    "\nINPUT is a coordinate file (mmCIF, PDB, etc)." },
  CommonUsage[Help],
  CommonUsage[Version],
  CommonUsage[Verbose],
  { Verbosity, 0, "", "verbosity", Arg::Int,
    "  --verbosity=I  \tVerbosity setting." },
  { Timing, 0, "", "timing", Arg::None,
    "  --timing  \tPrint how long individual steps take." },
  { GridSpac, 0, "s", "spacing", Arg::Float,
    "  -s, --spacing=D  \tMax. sampling for the grid (default: 1A)." },
  { GridDims, 0, "g", "grid", Arg::Int3,
    "  -g, --grid=NX,NY,NZ  \tGrid sampling." },
  { Radius, 0, "r", "radius", Arg::Float,
    "  -r, --radius=R  \tUse constant radius of atom spheres." },
  { RProbe, 0, "", "r-probe", Arg::Float,
    "  --r-probe=Rp  \tUse VdW radius + Rp (default: 1.0A)." },
  { RShrink, 0, "", "r-shrink", Arg::Float,
    "  --r-shrink=Rs  \tFinally, remove a shell of thickness Rs (default: 1.1A)." },
  { IslandLimit, 0, "", "island-limit", Arg::Float,
    "  --island-limit=VOL  \tRemove \"islands\" up to VOL A^3." },
  { Hydrogens, 0, "", "hydrogens", Arg::None,
    "  --hydrogens  \tDon't ignore hydrogens." },
  { AnyOccupancy, 0, "", "any-occupancy", Arg::None,
    "  --any-occupancy  \tDon't ignore zero-occupancy atoms." },
  { SetOccupancy, 0, "", "set-occupancy", Arg::None,
    "  --set-occupancy  \tSet values according to occupancy." },
  { CctbxCompat, 0, "", "cctbx-compat", Arg::None,
    "  --cctbx-compat  \tUse vdW, Rprobe, Rshrink radii from cctbx." },
  { RefmacCompat, 0, "", "refmac-compat", Arg::None,
    "  --refmac-compat  \tUse radii compatible with Refmac." },
  { Invert, 0, "I", "invert", Arg::None,
    "  -I, --invert  \t0 for solvent, 1 for molecule." },
  { 0, 0, 0, 0, 0, 0 }
};

} // anonymous namespace

int GEMMI_MAIN(int argc, char **argv) {
  OptParser p(EXE_NAME);
  p.simple_parse(argc, argv, Usage);
  p.require_positional_args(2);
  const char* input = p.nonOption(0);
  const char* output = p.nonOption(1);

  p.check_exclusive_pair(GridDims, GridSpac);
  p.check_exclusive_pair(CctbxCompat, RefmacCompat);
  p.check_exclusive_pair(Radius, RProbe);
  p.check_exclusive_pair(Radius, CctbxCompat);
  p.check_exclusive_pair(Radius, RefmacCompat);

  if (p.options[Verbose])
    std::fprintf(stderr, "Converting %s ...\n", input);

  try {
    Timer timer(p.options[Timing]);
    gemmi::Structure st = gemmi::read_structure_gz(input);
    gemmi::Ccp4<float> mask;

    mask.grid.unit_cell = st.cell;
    mask.grid.spacegroup = st.find_spacegroup();
    if (p.options[GridDims]) {
      auto dims = parse_comma_separated_ints(p.options[GridDims].arg);
      mask.grid.set_size(dims[0], dims[1], dims[2]);
    } else {
      double spac = 1;
      if (p.options[GridSpac])
        spac = std::atof(p.options[GridSpac].arg);
      mask.grid.set_size_from_spacing(spac, gemmi::GridSizeRounding::Up);
    }
    if (p.options[Verbose]) {
      const auto& g = mask.grid;
      std::fprintf(stderr, "Grid: %d x %d x %d\n", g.nu, g.nv, g.nw);
      std::fprintf(stderr, "Spacing: %.3f, %.3f, %.3f\n",
                           g.spacing[0], g.spacing[1], g.spacing[2]);
      size_t np = g.data.size();
      double vol = st.cell.volume;
      std::fprintf(stderr, "Total points: %zu\n", np);
      std::fprintf(stderr, "Unit cell volume: %.1f A^3\n", vol);
      std::fprintf(stderr, "Volume per point: %.3f A^3\n", vol / np);
      if (g.spacegroup) {
        std::fprintf(stderr, "Spacegroup: %s\n", g.spacegroup->hm);
        int na = g.spacegroup->operations().order();
        std::fprintf(stderr, "ASU volume: %.1f A^3\n", vol / na);
        std::fprintf(stderr, "Points per ASU: %.1f\n", double(np) / na);
      } else {
        std::fprintf(stderr, "No spacegroup\n");
      }
    }
    if (st.models.size() > 1)
      std::fprintf(stderr, "Note: only the first model is used.\n");

    gemmi::SolventMasker masker(gemmi::AtomicRadiiSet::VanDerWaals);
    if (p.options[Radius])
      masker.set_radii(gemmi::AtomicRadiiSet::Constant,
                       std::atof(p.options[Radius].arg));
    else if (p.options[CctbxCompat])
      masker.set_radii(gemmi::AtomicRadiiSet::Cctbx);
    else if (p.options[RefmacCompat])
      masker.set_radii(gemmi::AtomicRadiiSet::Refmac);

    if (p.options[RProbe])
      masker.rprobe = std::atof(p.options[RProbe].arg);
    if (p.options[RShrink])
      masker.rshrink = std::atof(p.options[RShrink].arg);
    if (p.options[Hydrogens])
      masker.ignore_hydrogen = false;
    if (p.options[AnyOccupancy])
      masker.ignore_zero_occupancy_atoms = false;
    if (p.options[SetOccupancy])
      masker.use_atom_occupancy = true;

    if (p.options[Verbose]) {
      masker.verbosity++;
    }
    if (p.options[Verbosity]) {
      masker.verbosity = std::atoi(p.options[Verbosity].arg);
    }

    timer.start();
    masker.clear(mask.grid);

    masker.mask_points(mask.grid, st.models[0]);

    timer.print("Points masked in");

    timer.start();
    masker.symmetrize(mask.grid);
    timer.print("Mask symmetrized in");

    if (masker.rshrink > 0) {
      timer.start();
      masker.shrink(mask.grid);
      timer.print("Mask shrunken in");
    }

    if (p.options[IslandLimit])
      masker.island_min_volume = std::atof(p.options[IslandLimit].arg);
    if (masker.island_min_volume > 0.) {
      timer.start();
      int n = 0;
      if (p.options[SetOccupancy]) {
        std::fprintf(stderr, "Creating intermediate grid for islands removal ... ");
        gemmi::Grid<int> g;
        g.unit_cell   = mask.grid.unit_cell;
        g.spacegroup  = mask.grid.spacegroup;
        g.nu          = mask.grid.nu;
        g.nv          = mask.grid.nv;
        g.nw          = mask.grid.nw;
        g.axis_order  = mask.grid.axis_order;
        g.spacing[0]  = mask.grid.spacing[0];
        g.spacing[1]  = mask.grid.spacing[1];
        g.spacing[2]  = mask.grid.spacing[2];
        gemmi::Vec3 inv_n(1.0 / g.nu, 1.0 / g.nv, 1.0 / g.nw);
        g.orth_n = g.unit_cell.orth.mat.multiply_by_diagonal(inv_n);
        // do this as a loop:
        for (int w = 0, idx = 0; w < g.nw; ++w) {
          for (int v = 0; v < g.nv; ++v) {
            for (int u = 0; u < g.nu; ++u, ++idx) {
              // turns our 0.0 ... 1.0 mask into a 0/1 one:
              g.data.push_back(std::round(mask.grid.data[idx]));
            }
          }
        }
        std::fprintf(stderr, "done\n");
        n = masker.remove_islands(g);
        int n1 = 0;
        int n2 = 0;
        for (int w = 0, idx = 0; w < g.nw; ++w) {
          for (int v = 0; v < g.nv; ++v) {
            for (int u = 0; u < g.nu; ++u, ++idx) {
              if (mask.grid.data[idx]>=0.5&&g.data[idx]==0) {
                // change from 1 -> 0
                mask.grid.data[idx] = 0.0;
                n1++;
              }
              else if (mask.grid.data[idx]<0.5&&g.data[idx]==1) {
                // change from 0 -> 1
                mask.grid.data[idx] = 1.0;
                n2++;
              }
            }
          }
        }
        if (n1>0) {
          std::fprintf(stderr, " # of grid points >=0.5 reset = %d\n",n1);
        }
        if (n2>0) {
          std::fprintf(stderr, " # of grid points < 0.5 reset = %d\n",n2);
        }
      }
      else {
        n = masker.remove_islands(mask.grid);
      }
      timer.print("Islands removed in");
      if (p.options[Verbose])
        std::fprintf(stderr, "Islands removed: %d\n", n);
    }

    if (p.options[Verbose]) {
      size_t n = std::count(mask.grid.data.begin(), mask.grid.data.end(), 0);
      std::fprintf(stderr, "Points masked by model: %zu\n", n);
    }

    if (p.options[Invert])
      masker.invert(mask.grid);

    if (masker.use_atom_occupancy) {
      mask.update_ccp4_header(2,true);
      mask.write_ccp4_map(output);
    }
    else {
      mask.update_ccp4_header(0);
      mask.write_ccp4_map(output, 0);
    }
  } catch (std::runtime_error& e) {
    std::fprintf(stderr, "ERROR: %s\n", e.what());
    return 1;
  }
  return 0;
}
