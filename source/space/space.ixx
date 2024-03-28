export module scribble.space;

namespace scribble::space {
	export class space_partition
	{
	public:
		virtual ~space_partition() {}

		virtual void update() = 0;
	};
}
