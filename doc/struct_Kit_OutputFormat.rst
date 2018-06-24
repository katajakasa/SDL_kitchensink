.. index:: pair: struct; Kit_OutputFormat
.. _doxid-struct_kit___output_format:
.. _cid-kit_outputformat:

struct Kit_OutputFormat
=======================

.. toctree::
	:hidden:

.. code-block:: cpp
	:class: overview-code-block

	#include <kitformat.h>


Overview
~~~~~~~~



.. ref-code-block:: cpp
	:class: overview-code-block

	// fields

	unsigned int :ref:`format<doxid-struct_kit___output_format_1a5d458a74effbd53125e25132e7bbe51a>`
	bool :ref:`is_signed<doxid-struct_kit___output_format_1abd9639f26071284d0089f5faa5cf9c35>`
	int :ref:`bytes<doxid-struct_kit___output_format_1ab498771bc7b91153549229f063c563d9>`
	int :ref:`samplerate<doxid-struct_kit___output_format_1ab93e998816c7838c471e923d0ecfa011>`
	int :ref:`channels<doxid-struct_kit___output_format_1ab644e71c41825fc55a7fa2bbf4e1e795>`
	int :ref:`width<doxid-struct_kit___output_format_1a69cf1282972c007c95fcffaf393de309>`
	int :ref:`height<doxid-struct_kit___output_format_1a8e0d763cd312c37baf5fd7e052ffaba5>`

.. _details-doxid-struct_kit___output_format:

Detailed Documentation
~~~~~~~~~~~~~~~~~~~~~~



Fields
------

.. _doxid-struct_kit___output_format_1a5d458a74effbd53125e25132e7bbe51a:
.. _cid-kit_outputformat::format:
.. ref-code-block:: cpp
	:class: title-code-block

	unsigned int format

SDL Format.

.. _doxid-struct_kit___output_format_1abd9639f26071284d0089f5faa5cf9c35:
.. _cid-kit_outputformat::is_signed:
.. ref-code-block:: cpp
	:class: title-code-block

	bool is_signed

Signedness (if audio)

.. _doxid-struct_kit___output_format_1ab498771bc7b91153549229f063c563d9:
.. _cid-kit_outputformat::bytes:
.. ref-code-block:: cpp
	:class: title-code-block

	int bytes

Bytes per sample per channel (if audio)

.. _doxid-struct_kit___output_format_1ab93e998816c7838c471e923d0ecfa011:
.. _cid-kit_outputformat::samplerate:
.. ref-code-block:: cpp
	:class: title-code-block

	int samplerate

Sampling rate (if audio)

.. _doxid-struct_kit___output_format_1ab644e71c41825fc55a7fa2bbf4e1e795:
.. _cid-kit_outputformat::channels:
.. ref-code-block:: cpp
	:class: title-code-block

	int channels

Channels (if audio)

.. _doxid-struct_kit___output_format_1a69cf1282972c007c95fcffaf393de309:
.. _cid-kit_outputformat::width:
.. ref-code-block:: cpp
	:class: title-code-block

	int width

Width in pixels (if video)

.. _doxid-struct_kit___output_format_1a8e0d763cd312c37baf5fd7e052ffaba5:
.. _cid-kit_outputformat::height:
.. ref-code-block:: cpp
	:class: title-code-block

	int height

Height in pixels (if video)

