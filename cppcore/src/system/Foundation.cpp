#include "system/Foundation.hpp"
#include "system/Shape.hpp"

#include <Eigen/Dense>  // for `colPivHouseholderQr()`

namespace tbm { namespace detail {

std::pair<Index3D, Index3D> find_bounds(Shape const& shape, Lattice const& lattice) {
    auto const ndim = lattice.vectors.size();
    auto const lattice_matrix = [&]{
        Eigen::MatrixXf m(ndim, ndim);
        for (auto i = 0u; i < ndim; ++i) {
            m.col(i) = lattice.vectors[i].head(ndim);
        }
        return m;
    }();

    Array3i lower_bound = Array3i::Constant(std::numeric_limits<int>::max());
    Array3i upper_bound = Array3i::Constant(std::numeric_limits<int>::min());
    for (auto const& point : shape.vertices) {
        // Translate Cartesian coordinates `p` into lattice vector coordinates `v`
        // -> solve `A*v = p`, where A is `lattice_matrix`
        auto const& p = point.head(ndim);
        Array3i v = Array3i::Zero();
        v.head(ndim) = lattice_matrix.colPivHouseholderQr().solve(p).cast<int>();

        lower_bound = (v < lower_bound).select(v, lower_bound);
        upper_bound = (v > upper_bound).select(v, upper_bound);
    }

    // Add +/- 1 padding to compensate for `cast<int>()` truncation
    lower_bound.head(ndim) -= 1;
    upper_bound.head(ndim) += 1;

    return {lower_bound, upper_bound};
}

CartesianArray generate_positions(Cartesian origin, Index3D size, Lattice const& lattice) {
    // The nested loops look messy, but it's the fastest way to calculate all the positions
    // because the intermediate a, b, c positions are reused.
    auto const num_sublattices = lattice.sublattices.size();
    auto const num_sites = static_cast<int>(size.prod() * num_sublattices);
    CartesianArray positions(num_sites);

    auto idx = 0;
    for (auto a = 0; a < size[0]; ++a) {
        Cartesian pa = origin + static_cast<float>(a) * lattice.vectors[0];
        for (auto b = 0; b < size[1]; ++b) {
            Cartesian pb = (b == 0) ? pa : pa + static_cast<float>(b) * lattice.vectors[1];
            for (auto c = 0; c < size[2]; ++c) {
                Cartesian pc = (c == 0) ? pb : pb + static_cast<float>(c) * lattice.vectors[2];
                for (auto sub = 0u; sub < num_sublattices; ++sub) {
                    positions[idx++] = pc + lattice[sub].offset;
                } // sub
            } // c
        } // b
    } // a

    return positions;
}

ArrayX<int16_t> count_neighbors(Foundation const& foundation) {
    ArrayX<int16_t> neighbor_count(foundation.get_num_sites());

    auto const& lattice = foundation.get_lattice();
    auto const size = foundation.get_size().array();

    for (auto const& site : foundation) {
        auto const& sublattice = lattice[site.get_sublattice()];
        auto num_neighbors = static_cast<int16_t>(sublattice.hoppings.size());

        // Reduce the neighbor count for sites on the edges
        for (auto const& hopping : sublattice.hoppings) {
            auto const index = (site.get_index() + hopping.relative_index).array();
            if (any_of(index < 0) || any_of(index >= size))
                num_neighbors -= 1;
        }

        neighbor_count[site.get_idx()] = num_neighbors;
    }

    return neighbor_count;
}

void clear_neighbors(Site& site, ArrayX<int16_t>& neighbor_count) {
    if (neighbor_count[site.get_idx()] == 0)
        return;

    site.for_each_neighbour([&](Site neighbor, Hopping) {
        if (!neighbor.is_valid())
            return;

        auto const neighbor_idx = neighbor.get_idx();
        neighbor_count[neighbor_idx] -= 1;
        if (neighbor_count[neighbor_idx] < site.get_lattice().min_neighbours) {
            neighbor.set_valid(false);
            // recursive call... but it will not be very deep
            clear_neighbors(neighbor, neighbor_count);
        }
    });

    neighbor_count[site.get_idx()] = 0;
}

void trim_edges(Foundation& foundation) {
    auto neighbor_count = count_neighbors(foundation);
    for (auto& site : foundation) {
        if (!site.is_valid()) {
            clear_neighbors(site, neighbor_count);
        }
    }
}

ArrayX<sub_id> make_sublattice_ids(Foundation const& foundation) {
    ArrayX<sub_id> sublattice_ids(foundation.get_num_sites());

    auto const max_id = static_cast<sub_id>(foundation.get_num_sublattices());
    for (auto i = 0; i < foundation.get_num_sites();) {
        for (auto id = sub_id{0}; id < max_id; ++id, ++i) {
            sublattice_ids[i] = id;
        }
    }

    return sublattice_ids;
}

} // namespace detail

Foundation::Foundation(Lattice const& lattice, Primitive const& primitive)
    : lattice(lattice),
      bounds(-primitive.size.array() / 2, (primitive.size.array() - 1) / 2),
      size(primitive.size),
      size_n(static_cast<int>(lattice.sublattices.size())),
      num_sites(size.prod() * size_n),
      positions(detail::generate_positions(lattice.calc_position(bounds.first), size, lattice)),
      is_valid(ArrayX<bool>::Constant(num_sites, true)) {}

Foundation::Foundation(Lattice const& lattice, Shape const& shape)
    : lattice(lattice),
      bounds(detail::find_bounds(shape, lattice)),
      size((bounds.second - bounds.first) + Index3D::Ones()),
      size_n(static_cast<int>(lattice.sublattices.size())),
      num_sites(size.prod() * size_n),
      positions(detail::generate_positions(lattice.calc_position(bounds.first, shape.offset),
                                           size, lattice)),
      is_valid(shape.contains(positions)) {
    detail::trim_edges(*this);
}

HamiltonianIndices::HamiltonianIndices(Foundation const& foundation)
    : indices(ArrayX<int>::Constant(foundation.get_num_sites(), -1)), num_valid_sites(0) {
    // Assign Hamiltonian indices to all valid sites
    auto& is_valid = foundation.get_states();
    for (auto i = 0; i < foundation.get_num_sites(); ++i) {
        if (is_valid[i])
            indices[i] = num_valid_sites++;
    }
}

} // namespace tbm
